#version 300 es
precision highp float; precision highp int; precision highp sampler2D; precision highp usampler2D;
in vec2 v; uniform sampler2D t; uniform vec2 d; out vec4 o;
void main(){vec3 c=texture(t,v+vec2(-d.x*2.,0.)).rgb+texture(t,v+vec2(-d.x,d.y)).rgb*2.+texture(t,v+vec2(0.,d.y*2.)).rgb+texture(t,v+vec2(d.x,d.y)).rgb*2.+texture(t,v+vec2(d.x*2.,0.)).rgb+texture(t,v+vec2(d.x,-d.y)).rgb*2.+texture(t,v+vec2(0.,-d.y*2.)).rgb+texture(t,v+vec2(-d.x,-d.y)).rgb*2.;o=vec4(c/12.,1.);}