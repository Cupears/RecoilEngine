#include <GLES3/gl31.h>
#include <SDL.h>
#include <android/log.h>
#include <stdbool.h>
#include <math.h>

#include "recoil_icon_shaders.h"
#include "camera_gestures.h"

static const char* const logTag = "RecoilGraphicsProbe";

static GLuint CompileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);

	GLint compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled == GL_TRUE)
		return shader;

	char message[1024] = {0};
	glGetShaderInfoLog(shader, sizeof(message), NULL, message);
	__android_log_print(ANDROID_LOG_ERROR, logTag, "Shader compile failed: %s", message);
	glDeleteShader(shader);
	return 0;
}

static GLuint CreateProgram(void)
{
	GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertexSource);
	GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
	GLuint program = 0;
	if (vertex && fragment) {
		program = glCreateProgram();
		glAttachShader(program, vertex);
		glAttachShader(program, fragment);
		glLinkProgram(program);

		GLint linked = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (linked != GL_TRUE) {
			char message[1024] = {0};
			glGetProgramInfoLog(program, sizeof(message), NULL, message);
			__android_log_print(ANDROID_LOG_ERROR, logTag, "Program link failed: %s", message);
			glDeleteProgram(program);
			program = 0;
		}
	}
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	return program;
}

static GLuint UploadMesh(GLuint program, const char* path, GLsizei* vertexCount, float** retainedVertices)
{
	size_t size = 0;
	void* mesh = SDL_LoadFile(path, &size);
	const size_t stride = 8 * sizeof(float);
	if (!mesh || size == 0 || size % (3 * stride) != 0 || size / stride > 1000000) {
		__android_log_print(ANDROID_LOG_ERROR, logTag, "Could not read %s: %s", path, SDL_GetError());
		SDL_free(mesh);
		return 0;
	}
	*vertexCount = (GLsizei)(size / stride);
	GLuint buffer = 0;
	glGenBuffers(1, &buffer);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, size, mesh, GL_STATIC_DRAW);
	if (retainedVertices)
		*retainedVertices = mesh;
	else
		SDL_free(mesh);

	GLint pos = glGetAttribLocation(program, "pos");
	GLint uvw = glGetAttribLocation(program, "uvw");
	GLint color = glGetAttribLocation(program, "color");
	glEnableVertexAttribArray(pos);
	glVertexAttribPointer(pos, 3, GL_FLOAT, GL_FALSE, stride, NULL);
	glEnableVertexAttribArray(uvw);
	glVertexAttribPointer(uvw, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(color);
	glVertexAttribPointer(color, 3, GL_FLOAT, GL_FALSE, stride, (const void*)(5 * sizeof(float)));
	return buffer;
}

static GLuint UploadUnitTexture(const char* path)
{
	const int side = 1024;
	size_t size = 0;
	void* pixels = SDL_LoadFile(path, &size);
	if (!pixels || size != (size_t)side * side * 4) {
		__android_log_print(ANDROID_LOG_ERROR, logTag, "Could not read %s: %s", path, SDL_GetError());
		SDL_free(pixels);
		return 0;
	}
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glGenerateMipmap(GL_TEXTURE_2D);
	SDL_free(pixels);
	return texture;
}

typedef struct { float x, y, z; } Point;

static Point Project(const float* matrix, const float* vertex)
{
	return (Point){
		matrix[0] * vertex[0] + matrix[4] * vertex[1] + matrix[8] * vertex[2] + matrix[12],
		matrix[1] * vertex[0] + matrix[5] * vertex[1] + matrix[9] * vertex[2] + matrix[13],
		matrix[2] * vertex[0] + matrix[6] * vertex[1] + matrix[10] * vertex[2] + matrix[14],
	};
}

static bool Weights(float x, float y, Point a, Point b, Point c, float* w)
{
	float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
	if (fabsf(det) < 1e-8f)
		return false;
	w[0] = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
	w[1] = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
	w[2] = 1 - w[0] - w[1];
	return w[0] >= -1e-5f && w[1] >= -1e-5f && w[2] >= -1e-5f;
}

static bool PickTerrain(const float* vertices, GLsizei count, const float* matrix, float x, float y, Point* target)
{
	float nearest = 1.0f;
	bool hit = false;
	for (GLsizei i = 0; i < count; i += 3) {
		const float* a = vertices + i * 8;
		const float* b = a + 8;
		const float* c = b + 8;
		Point pa = Project(matrix, a), pb = Project(matrix, b), pc = Project(matrix, c);
		float w[3];
		if (!Weights(x, y, pa, pb, pc, w))
			continue;
		float depth = w[0] * pa.z + w[1] * pb.z + w[2] * pc.z;
		if (depth < -1.0f || depth >= nearest)
			continue;
		nearest = depth;
		*target = (Point){w[0] * a[0] + w[1] * b[0] + w[2] * c[0], 0,
			w[0] * a[2] + w[1] * b[2] + w[2] * c[2]};
		hit = true;
	}
	return hit;
}

static float GroundHeight(const float* vertices, GLsizei count, Point position)
{
	for (GLsizei i = 0; i < count; i += 3) {
		const float* a = vertices + i * 8;
		const float* b = a + 8;
		const float* c = b + 8;
		float w[3];
		if (Weights(position.x, position.z, (Point){a[0], a[2], 0},
				(Point){b[0], b[2], 0}, (Point){c[0], c[2], 0}, w))
			return w[0] * a[1] + w[1] * b[1] + w[2] * c[1];
	}
	return position.y;
}

static int RunProbe(SDL_Window* window)
{
	__android_log_print(ANDROID_LOG_INFO, logTag, "GL %s | GPU %s | GLSL %s",
		(const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
		(const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));

	GLuint program = CreateProgram();
	if (!program)
		return 1;

	GLuint vaos[2] = {0};
	glGenVertexArrays(2, vaos);
	glBindVertexArray(vaos[0]);
	GLsizei vertexCount = 0;
	GLuint unit = UploadMesh(program, "unit-mesh.bin", &vertexCount, NULL);
	glBindVertexArray(vaos[1]);
	GLsizei terrainCount = 0;
	float* terrainVertices = NULL;
	GLuint terrain = UploadMesh(program, "terrain-mesh.bin", &terrainCount, &terrainVertices);
	GLuint buffers[] = {unit, terrain};
	GLuint textures[] = {UploadUnitTexture("unit-color.rgba"), UploadUnitTexture("unit-material.rgba"),
		UploadUnitTexture("terrain-color.rgba")};
	if (!unit || !terrain || !textures[0] || !textures[1] || !textures[2]) {
		glDeleteTextures(3, textures);
		glDeleteBuffers(2, buffers);
		glDeleteVertexArrays(2, vaos);
		glDeleteProgram(program);
		SDL_free(terrainVertices);
		return 1;
	}
	GLuint query = 0;
	glGenQueries(1, &query);

	glUseProgram(program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textures[0]);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, textures[1]);
	glUniform1i(glGetUniformLocation(program, "colorTexture"), 0);
	glUniform1i(glGetUniformLocation(program, "materialTexture"), 1);
	glUniform3f(glGetUniformLocation(program, "teamColor"), 1.0f, 0.48f, 0.12f);
	GLint terrainPass = glGetUniformLocation(program, "drawTerrain");
	GLint transform = glGetUniformLocation(program, "viewProjMat");
	glEnable(GL_DEPTH_TEST);
	bool running = true;
	bool checkedDraw = false;
	Point position = {0}, target = {0};
	CameraGesture camera = {.zoom = 1.0f};
	Uint64 lastTick = SDL_GetTicks64();
	int result = 0;

	while (running) {
		SDL_Event event;
		bool tapped = false;
		float tapX = 0, tapY = 0;
		int windowWidth = 0, windowHeight = 0;
		SDL_GetWindowSize(window, &windowWidth, &windowHeight);
		float touchAspect = windowHeight > 0 ? (float)windowWidth / windowHeight : 1;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				running = false;
			if (event.type == SDL_APP_WILLENTERBACKGROUND ||
			    (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)) {
				camera.count = 0;
				camera.canTap = false;
				tapped = false;
			}
			if (event.type != SDL_FINGERDOWN && event.type != SDL_FINGERMOTION && event.type != SDL_FINGERUP)
				continue;
			TouchPoint touch = {event.tfinger.x, event.tfinger.y};
			if (event.type == SDL_FINGERDOWN)
				CameraTouchDown(&camera, event.tfinger.fingerId, touch);
			if (event.type == SDL_FINGERMOTION)
				CameraTouchMove(&camera, event.tfinger.fingerId, touch, touchAspect);
			if (event.type == SDL_FINGERUP &&
			    CameraTouchUp(&camera, event.tfinger.fingerId, touch, touchAspect)) {
				tapped = true;
				tapX = 2 * touch.x - 1;
				tapY = 1 - 2 * touch.y;
			}
		}

		int width = 0;
		int height = 0;
		SDL_GL_GetDrawableSize(window, &width, &height);
		if (width <= 0 || height <= 0) {
			SDL_Delay(16);
			continue;
		}

		const float angle = 0.65f;
		float c = cosf(angle);
		float s = sinf(angle);
		const float scale = 0.84f;
		float aspect = (float)height / (float)width;
		float screenScale = scale * camera.zoom;
		const float matrix[16] = {
			c * screenScale * aspect, -0.77f * s * screenScale, -0.638f * s * scale, 0,
			0, 0.638f * screenScale, -0.77f * scale, 0,
			s * screenScale * aspect, 0.77f * c * screenScale, 0.638f * c * scale, 0,
			camera.pan.x, camera.pan.y, 0, 1,
		};
		if (tapped)
			PickTerrain(terrainVertices, terrainCount, matrix, tapX, tapY, &target);
		Uint64 now = SDL_GetTicks64();
		float elapsed = fminf((now - lastTick) * 0.001f, 0.05f);
		lastTick = now;
		float dx = target.x - position.x, dz = target.z - position.z;
		float distance = hypotf(dx, dz);
		if (distance > 0) {
			float fraction = fminf(1.0f, elapsed * 0.9f / distance);
			position.x += dx * fraction;
			position.z += dz * fraction;
		}
		position.y = GroundHeight(terrainVertices, terrainCount, position);
		glViewport(0, 0, width, height);
		glClearColor(0.06f, 0.08f, 0.12f, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUniformMatrix4fv(transform, 1, GL_FALSE, matrix);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures[2]);
		glBindVertexArray(vaos[1]);
		glUniform1i(terrainPass, GL_TRUE);
		glDrawArrays(GL_TRIANGLES, 0, terrainCount);

		float unitMatrix[16];
		SDL_memcpy(unitMatrix, matrix, sizeof(matrix));
		for (int i = 0; i < 12; ++i)
			unitMatrix[i] *= 0.46f;
		const float offset[] = {position.x, position.y, position.z};
		Point projected = Project(matrix, offset);
		unitMatrix[12] = projected.x;
		unitMatrix[13] = projected.y;
		unitMatrix[14] = projected.z;
		glUniformMatrix4fv(transform, 1, GL_FALSE, unitMatrix);
		glBindTexture(GL_TEXTURE_2D, textures[0]);
		glBindVertexArray(vaos[0]);
		glUniform1i(terrainPass, GL_FALSE);
		if (!checkedDraw)
			glBeginQuery(GL_ANY_SAMPLES_PASSED, query);
		glDrawArrays(GL_TRIANGLES, 0, vertexCount);
		if (!checkedDraw) {
			glEndQuery(GL_ANY_SAMPLES_PASSED);
			GLuint visible = GL_FALSE;
			glGetQueryObjectuiv(query, GL_QUERY_RESULT, &visible);
			GLenum error = glGetError();
			if (error != GL_NO_ERROR || !visible) {
				__android_log_print(ANDROID_LOG_ERROR, logTag, "Unit draw failed: visible=%u; GL error 0x%x", visible, error);
				result = 1;
				break;
			}
			__android_log_print(ANDROID_LOG_INFO, logTag, "Drew Legion Flak Bot (%d triangles)", vertexCount / 3);
			checkedDraw = true;
		}

		SDL_GL_SwapWindow(window);
		SDL_Delay(16);
	}

	glDeleteQueries(1, &query);
	glDeleteTextures(3, textures);
	glDeleteBuffers(2, buffers);
	glDeleteVertexArrays(2, vaos);
	glDeleteProgram(program);
	SDL_free(terrainVertices);
	return result;
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		__android_log_print(ANDROID_LOG_ERROR, logTag, "SDL init failed: %s", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_Window* window = SDL_CreateWindow("Recoil Graphics Probe", SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED, 800, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
	int result = 1;
	if (context) {
		result = RunProbe(window);
		SDL_GL_DeleteContext(context);
	} else {
		__android_log_print(ANDROID_LOG_ERROR, logTag, "SDL window or GLES context failed: %s", SDL_GetError());
	}
	if (window)
		SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
