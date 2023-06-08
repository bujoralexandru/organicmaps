attribute vec4 a_pos;
attribute vec3 a_normal;
attribute vec2 a_texcoord; // new attribute for texture coordinates

uniform mat4 u_transform;

varying vec2 v_texcoord; // reorder: declare v_texcoord before v_intensity
varying vec2 v_intensity;

const vec3 lightDir = vec3(0.316, 0.0, 0.948);

void main()
{
  vec4 position = u_transform * vec4(a_pos.xyz, 1.0);
  v_intensity = vec2(max(0.0, -dot(lightDir, a_normal)), a_pos.w);
  v_texcoord = a_texcoord; // pass texture coordinates to fragment shader
  gl_Position = position;
#ifdef VULKAN
  gl_Position.y = -gl_Position.y;
  gl_Position.z = (gl_Position.z  + gl_Position.w) * 0.5;
#endif
}

