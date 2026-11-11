#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec3 aP; in vec2 aK; uniform mat4 uVP; uniform mat3 uHead; out vec2 vK; out float vY;
void main(){vec3 pv=vec3(0.,-.58,-.04);float w=smoothstep(-.96,-.52,aP.y);vec3 p=mix(aP,uHead*(aP-pv)+pv,w);vK=aK;vY=aP.y;gl_Position=uVP*vec4(p,1.);}