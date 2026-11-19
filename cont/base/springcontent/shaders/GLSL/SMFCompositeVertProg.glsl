#version 130

out vec2 texCoords;

void main() {
	texCoords = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	gl_Position = vec4(texCoords * 2.0 - 1.0, 0.0, 1.0);
}
