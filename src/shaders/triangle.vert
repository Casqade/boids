#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 outColor;


void main()
{
  gl_PointSize = 1.0 + floor(5.0 - inPosition.z * 5.0);

  gl_Position = vec4(
    (inPosition.xyz - vec3(0.5, 0.5, 0.5)) * 2,
    1.0 );

  outColor = (1.0 - inPosition.z) * inColor;
}

