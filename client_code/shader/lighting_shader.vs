#version 330 core
layout ( location = 0 ) in vec3 aPos;
layout ( location = 1 ) in vec3 aNormal;
layout ( location = 2 ) in vec2 aTextCoord;

out vec3 Normal;
out vec3 FragPos;
out vec2 TextCoord;

uniform mat4 model;
uniform mat3 normalMatrix;
uniform mat4 view;
uniform mat4 projection;

void main() {
    Normal = normalMatrix * aNormal;
    FragPos = vec3( model * vec4(aPos,1.0) );
    TextCoord = aTextCoord;

    gl_Position = projection * view * model * vec4(aPos , 1.0);
}