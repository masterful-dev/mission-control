#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec2 v; uniform sampler2D t; uniform vec2 d; uniform float uThr; out vec4 o;
void main(){vec3 c=texture(t,v).rgb*4.;c+=texture(t,v-d).rgb+texture(t,v+d).rgb+texture(t,v+vec2(d.x,-d.y)).rgb+texture(t,v-vec2(d.x,-d.y)).rgb;c/=8.;if(any(isnan(c))||any(isinf(c)))c=vec3(0.);
 if(uThr>0.){float l=max(c.r,max(c.g,c.b));c*=smoothstep(uThr*.5,uThr*1.6,l);}o=vec4(c,1.);}