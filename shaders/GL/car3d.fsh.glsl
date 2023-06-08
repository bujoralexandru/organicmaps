varying vec2 v_texcoord; // reorder: declare v_texcoord before v_intensity
varying vec2 v_intensity;

#ifdef SAMSUNG_GOOGLE_NEXUS
uniform sampler2D u_colorTex;
#endif

uniform vec4 u_color;
uniform sampler2D u_texture; // new uniform for the texture

void main()
{
  vec4 resColor = vec4((v_intensity.x * 0.5 + 0.5) * u_color.rgb, u_color.a);
  gl_FragColor = vec4(v_texcoord, 0.0, 1.0); // use texture coordinates as color
}

