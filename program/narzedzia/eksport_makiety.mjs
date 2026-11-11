// Dane z makiety do C++: teksty, skorki, przykladowe modele i energia dla symulatora. Wynik: zrodla/gen_dane.inc
import fs from "node:fs"; import vm from "node:vm"; import path from "node:path";
const D = path.resolve(import.meta.dirname, ".."), M = path.resolve(D, "../makieta-v2"), A = M + "/archiwum-przed-trzecia-twarza/2026-11-05-arbiter";
const c = { window: {} }; vm.createContext(c);
for (const f of [A + "/i18n.js", M + "/skins.js", M + "/data-models.js", M + "/data-energy.js"]) vm.runInContext(fs.readFileSync(f, "utf8"), c);
const w = c.window, raw = (n, v) => `static const char* ${n} = R"JSON(${JSON.stringify(v)})JSON";\n`;
fs.writeFileSync(D + "/zrodla/gen_dane.inc", "// Wygenerowane: narzedzia/eksport_makiety.mjs\n" + raw("GEN_TEKSTY", w.I18N) + raw("GEN_SKORKI", w.SKINS) + raw("GEN_MODELE", w.MODELS) + raw("GEN_ENERGIA", w.ENERGY));
console.log(Object.keys(w.I18N.en).length, "tekstow,", Object.keys(w.SKINS).length, "skorek,", w.MODELS.length, "modeli");
