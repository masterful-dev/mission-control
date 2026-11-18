"""Zbieranie pomiarow sprzetu: NVML + hwmon + psutil.

NVML jest otwierany raz (leniwie) i trzymany otwarty — odczyt < 20 ms.
Numeracja kart zawsze wg kolejnosci PCI (CUDA_DEVICE_ORDER=PCI_BUS_ID).
"""
from __future__ import annotations

import os
import socket
import time
from pathlib import Path

import psutil

os.environ.setdefault("CUDA_DEVICE_ORDER", "PCI_BUS_ID")

import pynvml  # noqa: E402

DISKS = ["/mnt/H", "/mnt/G", "/mnt/F", "/"]
HWMON_ROOT = Path("/sys/class/hwmon")

_nvml_open = False
_static_gpu_info: dict[int, dict] = {}
_gpu_read_cache: dict[int, dict] = {}
_nvme_cache: dict = {}
_psu_cache: dict = {}
_rapl_cache: dict = {}
_net_cache: dict = {}

GPU_SLOW_TTL = 2.0   # s — zegary, wentylator
GPU_PROC_TTL = 1.0   # s — lista procesow na karcie
_gpu_slow_cache: dict = {}
_gpu_proc_cache: dict = {}
GPU_READ_TTL = 0.2  # s — odczyty NVML bywaja wolne (karta graficzna T400 ~10 ms/karta)


def _cached(store: dict, key, ttl: float, fn):
    """Cache odczytow wolno zmieniajacych sie (nazwy/limity GPU, hwmon PSU/NVMe),
    zeby pelny snapshot miescil sie w budzecie czasowym."""
    now = time.monotonic()
    hit = store.get(key)
    if hit and now - hit[0] < ttl:
        return hit[1]
    val = fn()
    store[key] = (now, val)
    return val


def _nvml_init() -> bool:
    global _nvml_open
    if _nvml_open:
        return True
    try:
        pynvml.nvmlInit()
        _nvml_open = True
    except pynvml.NVMLError:
        _nvml_open = False
    return _nvml_open


def _rd(handle, fn, *args):
    """Jeden odczyt NVML — bledy pojedynczego pola nie wywala catej telemetrii."""
    try:
        return fn(handle, *args)
    except pynvml.NVMLError:
        return None


def _field_temp(handle, field_id) -> float | None:
    try:
        vals = pynvml.nvmlDeviceGetFieldValues(handle, [field_id])
    except pynvml.NVMLError:
        return None
    for v in vals:
        if v.nvmlReturn == pynvml.NVML_SUCCESS:
            return float(v.value.uiVal)
    return None


def gpu_processes(handle) -> list[dict]:
    procs: dict[int, dict] = {}
    for getter in (pynvml.nvmlDeviceGetComputeRunningProcesses_v3,
                   pynvml.nvmlDeviceGetGraphicsRunningProcesses_v3):
        try:
            for p in getter(handle):
                e = procs.setdefault(p.pid, {"pid": p.pid, "vram_mb": 0})
                if p.usedGpuMemory:
                    e["vram_mb"] += p.usedGpuMemory // (1024 * 1024)
        except pynvml.NVMLError:
            pass
    for e in procs.values():
        try:
            e["name"] = psutil.Process(e["pid"]).name()
        except psutil.Error:
            e["name"] = "?"
    return sorted(procs.values(), key=lambda e: -e["vram_mb"])


def _static_info(i: int, h) -> dict:
    try:
        name = pynvml.nvmlDeviceGetName(h)
    except pynvml.NVMLError:
        name = f"GPU{i}"
    if isinstance(name, bytes):
        name = name.decode()
    limit = _rd(h, pynvml.nvmlDeviceGetEnforcedPowerLimit)
    return {
        "name": name,
        "uuid": _rd(h, pynvml.nvmlDeviceGetUUID) or "",
        "power_limit_w": round(limit / 1000, 1) if limit is not None else None,
        "nvlink": _nvlink(h),
    }


def read_gpus() -> list[dict]:
    return _cached(_gpu_read_cache, "gpus", GPU_READ_TTL, _read_gpus)


def _read_gpus() -> list[dict]:
    if not _nvml_init():
        return []
    out = []
    try:
        count = pynvml.nvmlDeviceGetCount()
    except pynvml.NVMLError:
        return []
    for i in range(count):
        try:
            h = pynvml.nvmlDeviceGetHandleByIndex(i)
        except pynvml.NVMLError:
            continue
        st = _cached(_static_gpu_info, i, 30.0, lambda h=h, i=i: _static_info(i, h))
        mem = _rd(h, pynvml.nvmlDeviceGetMemoryInfo)
        util = _rd(h, pynvml.nvmlDeviceGetUtilizationRates)
        power = _rd(h, pynvml.nvmlDeviceGetPowerUsage)
        temp = _rd(h, pynvml.nvmlDeviceGetTemperature, pynvml.NVML_TEMPERATURE_GPU)
        # pola wolnozmienne co GPU_SLOW_TTL (ClockInfo ~1,7 ms/wywolanie; wentylator na V100 SXM2 rzuca NotSupported)
        fan, clocks, mem_clock = _cached(_gpu_slow_cache, i, GPU_SLOW_TTL, lambda h=h: (
            _rd(h, pynvml.nvmlDeviceGetFanSpeed),
            _rd(h, pynvml.nvmlDeviceGetClockInfo, pynvml.NVML_CLOCK_SM),
            _rd(h, pynvml.nvmlDeviceGetClockInfo, pynvml.NVML_CLOCK_MEM)))
        out.append({
            "index": i,
            "name": st["name"],
            "uuid": st["uuid"],
            "vram_used_mb": int(mem.used // (1024 * 1024)) if mem else 0,
            "vram_total_mb": int(mem.total // (1024 * 1024)) if mem else 0,
            "temp_c": temp,
            "hbm_temp_c": _field_temp(h, pynvml.NVML_FI_DEV_MEMORY_TEMP),
            "power_w": round(power / 1000, 1) if power is not None else None,
            "power_limit_w": st["power_limit_w"],
            "util_gpu": util.gpu if util else None,
            "util_mem": util.memory if util else None,
            "sm_clock_mhz": clocks,
            "mem_clock_mhz": mem_clock,
            "fan_pct": fan,
            "nvlink": st["nvlink"],
            "processes": _cached(_gpu_proc_cache, i, GPU_PROC_TTL, lambda h=h: gpu_processes(h)),
        })
    return out


def _nvlink(h) -> bool:
    try:
        return bool(pynvml.nvmlDeviceGetNvLinkState(h, 0))
    except pynvml.NVMLError:
        return False


def read_cpu() -> dict:
    temps = {"package": None, "cores": [], "hottest": None}
    for d in _hwmon_devices("coretemp"):
        for label, val in _hwmon_temps(d):
            if label.lower().startswith("package"):
                temps["package"] = val
            elif label.lower().startswith("core"):
                temps["cores"].append(val)
    if temps["cores"]:
        temps["hottest"] = max(temps["cores"])
    freq = None
    try:
        f = psutil.cpu_freq()
        freq = round(f.current, 0) if f and f.current else None
    except Exception:
        pass
    return {
        "percent": psutil.cpu_percent(interval=None),
        "cores": psutil.cpu_count(logical=True),
        "load1": round(os.getloadavg()[0], 2),
        "temp_package_c": temps["package"],
        "temp_hottest_c": temps["hottest"],
        "freq_mhz": freq,
    }


def read_ram() -> dict:
    vm = psutil.virtual_memory()
    sw = psutil.swap_memory()
    return {
        "total_gb": round(vm.total / 2**30, 1),
        "used_gb": round(vm.used / 2**30, 1),
        "available_gb": round(vm.available / 2**30, 1),
        "free_gb": round(vm.free / 2**30, 1),
        "percent": vm.percent,
        "swap_total_gb": round(sw.total / 2**30, 1),
        "swap_used_gb": round(sw.used / 2**30, 1),
        "swap_percent": sw.percent,
    }


def read_disks() -> list[dict]:
    out = []
    for path in DISKS:
        try:
            u = psutil.disk_usage(path)
        except OSError:
            continue
        out.append({
            "path": path,
            "total_gb": round(u.total / 2**30, 1),
            "used_gb": round(u.used / 2**30, 1),
            "free_gb": round(u.free / 2**30, 1),
            "percent": u.percent,
        })
    return out


def read_nvme() -> list[dict]:
    return _cached(_nvme_cache, "nvme", 2.0, _read_nvme)


def _read_nvme() -> list[dict]:
    out = []
    for d in _hwmon_devices("nvme"):
        temp = None
        for label, val in _hwmon_temps(d):
            if label.lower() in ("composite", "temp1", "temp_input"):
                temp = val
                break
        if temp is None:
            temps = [v for _, v in _hwmon_temps(d)]
            temp = max(temps) if temps else None
        out.append({"name": d.name, "path": str(d), "temp_c": temp})
    return out


def read_psu() -> dict:
    return _cached(_psu_cache, "psu", 1.0, _read_psu)


def _read_psu() -> dict:
    psu = {"power_w": None, "temps": {}, "fans": []}
    for d in _hwmon_devices("corsairpsu"):
        for label, val in _hwmon_powers(d):
            if "total" in label.lower():
                psu["power_w"] = val
        for label, val in _hwmon_temps(d):
            psu["temps"][label] = val
        for label, val in _hwmon_fans(d):
            psu["fans"].append({"label": label, "rpm": val})
    return psu


def read_network() -> dict:
    """Przepustowość sieci = delta liczników / delta czasu (poprzedni odczyt cache'owany)."""
    io = psutil.net_io_counters()
    now, rx_mb, tx_mb = time.monotonic(), io.bytes_recv / 2**20, io.bytes_sent / 2**20
    prev = _net_cache.get("io")
    dt = now - prev[0] if prev else 0.0
    net = {
        "rx_mb": round(rx_mb, 1),
        "tx_mb": round(tx_mb, 1),
        "rx_mb_s": round((rx_mb - prev[1]) / dt, 2) if prev and dt > 0 else 0.0,
        "tx_mb_s": round((tx_mb - prev[2]) / dt, 2) if prev and dt > 0 else 0.0,
        "hostname": socket.gethostname(),
    }
    _net_cache["io"] = (now, rx_mb, tx_mb)
    return net


# ---------- energia (RAPL — szacunek CPU, gdy brak czujnika PSU) ----------

RAPL_PKG = Path("/sys/class/powercap/intel-rapl:0/energy_uj")
RAPL_MAX = Path("/sys/class/powercap/intel-rapl:0/max_energy_range_uj")


def read_rapl() -> dict:
    """Zużycie CPU z RAPL: energy_uj to licznik energii (µJ) z zerowaniem przy przekroczeniu
    max_energy_range — delta z skipem po przepełnieniu daje moc [W] z realnego Δt."""
    def czytaj():
        try:
            e = int(RAPL_PKG.read_text().strip())
            mx = int(RAPL_MAX.read_text().strip())
        except (OSError, ValueError):
            return None
        now = time.monotonic()
        prev = _rapl_cache.get("e")
        watt = None
        if prev:
            dt = now - prev[0]
            d = e - prev[1]
            if dt > 0 and 0 <= d <= mx:      # ujemna/ogromna delta = przepełnienie licznika
                watt = d / 1e6 / dt
        _rapl_cache["e"] = (now, e)
        return watt
    return {"watt": czytaj()}


# ---------- hwmon ----------

_hwmon_dev_cache: dict = {}


def _hwmon_devices(*names) -> list[Path]:
    """Lista urzadzen hwmon zmienia sie tylko przy (od)podlaczeniu sprzetu — cache 60 s."""
    return _cached(_hwmon_dev_cache, names, 60.0, lambda: _hwmon_devices_read(*names))


def _hwmon_devices_read(*names) -> list[Path]:
    out = []
    if not HWMON_ROOT.is_dir():
        return out
    for d in sorted(HWMON_ROOT.iterdir()):
        try:
            name = (d / "name").read_text().strip()
        except OSError:
            continue
        if not names or name in names:
            out.append(d)
    return out


def _read_input(f: Path, scale: float = 1000.0) -> float | None:
    try:
        return int(f.read_text().strip()) / scale
    except (OSError, ValueError):
        return None


_label_cache: dict = {}


def _label_of(d: Path, input_file: Path) -> str:
    """Etykiety czujnikow sie nie zmieniaja — czytane raz (24 rdzenie = 24 pliki na tick mniej)."""
    if input_file in _label_cache:
        return _label_cache[input_file]
    _label_cache[input_file] = v = _label_of_read(d, input_file)
    return v


def _label_of_read(d: Path, input_file: Path) -> str:
    label = input_file.with_name(input_file.name.replace("_input", "_label"))
    try:
        return label.read_text().strip()
    except OSError:
        return input_file.stem


def _hwmon_values(d: Path, suffix: str, scale: float = 1000.0) -> list[tuple[str, float]]:
    files = sorted(d.glob(f"*/{suffix}*_input")) + sorted(d.glob(f"{suffix}*_input"))
    out = []
    for f in files:
        val = _read_input(f, scale)
        if val is not None:
            out.append((_label_of(d, f), val))
    return out


def _hwmon_temps(d: Path):
    return _hwmon_values(d, "temp")


def _hwmon_powers(d: Path):
    return _hwmon_values(d, "power", scale=1e6)  # µW -> W


def _hwmon_fans(d: Path):
    out = []
    files = sorted(d.glob("*/fan*_input")) + sorted(d.glob("fan*_input"))
    for f in files:
        try:
            out.append((_label_of(d, f), int(f.read_text().strip())))
        except (OSError, ValueError):
            pass
    return out


# ---------- zbiorczy stan ----------

def snapshot(models: list[dict] | None = None, orphans: list[dict] | None = None) -> dict:
    """Pelny stan systemu — struktura identyczna dla /api/state i /ws."""
    net = read_network()
    return {
        "t": time.time(),
        "gpus": read_gpus(),
        "cpu": read_cpu(),
        "ram": read_ram(),
        "disks": read_disks(),
        "psu": read_psu(),
        "nvme": read_nvme(),
        "net": net,
        "models": models or [],
        "orphans": orphans or [],
    }


def read_energia() -> dict:
    """Surowe źródła mocy dla dziennika energii (§12a) — te same cache'owane odczyty co w /ws,
    więc wątek energii co 1 s nie skanuje hwmon/NVML od nowa."""
    psu = read_psu()
    gpus = read_gpus()
    return {
        "t": time.time(),
        "psu_w": psu.get("power_w"),
        "gpu_w": {g["index"]: g.get("power_w") for g in gpus},
        "rapl_w": read_rapl().get("watt"),
    }
