#version 450

layout(location = 0) in vec3 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
  if ( distance(gl_PointCoord, vec2(0.5, 0.5)) > 0.5 )
    discard;

  outColor = vec4(inColor, 1.0);
}

