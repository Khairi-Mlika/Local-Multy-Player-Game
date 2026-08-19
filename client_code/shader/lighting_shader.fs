#version 330 core

struct Light {
    vec3    direction;

    vec3    ambient;
    vec3    diffuse;
    vec3    specular;
};

struct Material {
    vec3        ambient;
    vec3        diffuse;
    vec3        specular;

    float       shininess;
};

in vec3 Normal;
in vec3 FragPos;
in vec2 TextCoord;

out vec4 FragColor;

uniform vec3 viewPos;

uniform Material material;
uniform sampler2D texture_diffuse1;
uniform Light light;

vec3 texColor = texture(texture_diffuse1, TextCoord).rgb;

vec3 directLight() {
    // ambient 
    vec3 ambient = light.ambient * texColor;

    // diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(-light.direction);

    float diff = max(dot(norm, lightDir) , 0.0);
    vec3 diffuse = light.diffuse * diff * texColor;

    // specular 
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir , norm);

    float spec = pow(max(dot(viewDir , reflectDir), 0.0) , 16);
    vec3 specular = light.specular * spec * vec3(0.5) ;

    return ambient + diffuse + specular;
}

float near = 0.1; 
float far  = 100.0; 
  
float LinearizeDepth(float depth) 
{
    float z = depth * 2.0 - 1.0; 
    return (2.0 * near * far) / (far + near - z * (far - near));	
}

void main() {
    float depth = LinearizeDepth(gl_FragCoord.z)/ (far*5);
    float fade = 1 - depth;
    vec3 lighting = directLight();
    FragColor = vec4( texColor * directLight() , 1.0);
}