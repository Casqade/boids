#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 outColor;

const mat4 mvp = mat4(
  vec4(0.3472704291343689, 0.0, 0.0, 0.0),
  vec4(0.0, 0.6173696517944336, 0.0, 0.0),
  vec4(0.0, 0.0, -1.0050251483917236, -1.0),
  vec4(0.0, 0.0, -0.01005025114864111, 0.0)
);

const float minPointSize = 1.0;
const float maxPointSize = 5.0;

void main()
{
  gl_PointSize = minPointSize + floor(inPosition.z * maxPointSize);

  gl_Position = mvp * vec4(
    (inPosition.xyz - vec3(0.5, 0.5, 1.116025404)), // 1.366025404 / 0.8660254038
    1.0 );

  outColor = (1.0 - inPosition.z) * inColor;
}

