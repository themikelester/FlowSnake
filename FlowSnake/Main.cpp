#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <GL\GL.h>
#include <math.h>  // sqrt
#include <stdio.h> // _vsnwprintf_s. Can disable for Release
#include "glext.h" // glGenBuffers, glBindBuffers, ...


/********** Defines *******************************/
#define countof(x) (sizeof(x)/sizeof(x[0])) // Defined in stdlib, but define here to avoid the header cost
#ifdef _DEBUG
#	define IFC(x) if (FAILED(hr = x)) { char buf[256]; sprintf_s(buf, "IFC Failed at Line %u\n", __LINE__); OutputDebugString(buf); goto Cleanup; }
#	define ASSERT(x) if (!(x)) { OutputDebugString("Assert Failed!\n"); DebugBreak(); }
#else
#	define IFC(x) if (FAILED(hr = x)) { goto Cleanup; }
#	define ASSERT(x)
#endif

#define uint UINT

/********** Function Declarations *****************/
LRESULT WINAPI MsgHandler(HWND hWnd, uint msg, WPARAM wParam, LPARAM lParam);
void Error(const char* pStr, ...);
float rand();

PFNGLGENBUFFERSPROC glGenBuffers;
PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLDELETEPROGRAMPROC glDeleteProgram;
PFNGLUSEPROGRAMPROC	   glUseProgram;  
PFNGLATTACHSHADERPROC  glAttachShader;  
PFNGLLINKPROGRAMPROC   glLinkProgram;   
PFNGLGETPROGRAMIVPROC  glGetProgramiv;  
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
PFNGLCREATESHADERPROC glCreateShader; 
PFNGLDELETESHADERPROC glDeleteShader; 
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLGETSHADERIVPROC glGetShaderiv;

/********** Structure Definitions******************/
struct float2
{
	float x;
	float y;

	float getLength()
	{
		if (x == 0 && y == 0)
			return 0;
		else
			return sqrt(x*x + y*y);
	}

	float2 operator- (float2 a)
	{
		float2 ret = {x - a.x, y - a.y};
		return ret;
	}

	float2 operator/ (float a)
	{
		float2 ret = {x/a, y/a};
		return ret;
	}
	
	float2 operator* (float a)
	{
		float2 ret = {x*a, y*a};
		return ret;
	}
};

// TODO: this can be packed into one short
struct Node
{
	bool hasParent;
	bool hasChild;
	short target;
	short tail;
	float2 velocity;
};

/********** Global Constants***********************/
const uint g_numVerts = 20;
const float g_tailDist = 0.01f;
const float g_speed = 0.1f; // in Screens per second

/********** Globals Variables *********************/
// Use SOA instead of AOS from optimal cache usage
// We'll traverse each array linearly in each stage of the algorithm
float2 g_positions[g_numVerts] = {};
Node   g_nodes[g_numVerts] = {};

GLuint g_vboPos;

/**************************************************/

inline bool IsValidTarget(Node& target, Node& current)
{
	bool validTarget = 
		 (target.hasChild == false) &&			// It can't already have a child 
		 (target.tail != current.tail);	// It can't be part of ourself

	return validTarget;
}

HRESULT FindNearestNeighbors()
{
	for (uint i = 0; i < g_numVerts; i++)
	{
		Node& n = g_nodes[i];

		if (n.hasParent == false) // Find the nearest potential parent
		{
			short nearest = -1;
			float minDist = 1.0f;

			for (uint j = 0; j < g_numVerts; j++)
			{
				Node& target = g_nodes[j];

				if (IsValidTarget(target, n))
				{
					float dist = (g_positions[j] - g_positions[i]).getLength();
					if (dist < minDist)
					{
						minDist = dist;
						nearest = j;
					}
				}
			}

			if (nearest == -1)
				return E_FAIL; // no valid targets found

			g_nodes[i].target = nearest;
		}
	}

	return S_OK;
}

HRESULT Chomp(short chomperIndex)
{
	Node& chomper = g_nodes[chomperIndex];
	Node& tail = g_nodes[chomper.target];

	chomper.hasParent = true;
	tail.hasChild = true;
	tail.tail = chomper.tail;

	// Update the whole chain's tail
	Node* pTarget = &tail;
	while (pTarget->hasParent)
	{
		pTarget = &g_nodes[pTarget->target];
		pTarget->tail = chomper.tail;
	}

	return S_OK;
}

HRESULT Update(uint deltaTime)
{
	HRESULT hr = S_OK;
	glClearColor(0.1f, 0.1f, 0.2f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	IFC( FindNearestNeighbors() );

	for (uint i = 0; i < g_numVerts; i++)
	{
		Node& currentNode = g_nodes[i];
		Node& targetNode = g_nodes[currentNode.target];

		// Determine velocity
		float2 targetVector = g_positions[currentNode.target] - g_positions[i];
		float  targetDist = targetVector.getLength();
		float2 targetDir = targetVector / targetDist;
		currentNode.velocity = targetDir * g_speed;

		// Check for chomps
		if (targetDist <= g_tailDist && IsValidTarget(targetNode, currentNode))
		{
			Chomp(i);
		}
	}

	// Determine positions
	for (uint i = 0; i < g_numVerts; i++)
	{
		g_positions[i].x += g_nodes[i].velocity.x * deltaTime/1000000.0f;
		g_positions[i].y += g_nodes[i].velocity.y * deltaTime/1000000.0f;
	}

Cleanup:
	return hr;
}

HRESULT Render()
{
	glBindBuffer(GL_ARRAY_BUFFER, g_vboPos);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_positions), g_positions, GL_STREAM_DRAW); 

	glDrawArrays(GL_POINTS, 0, g_numVerts);
	return S_OK;
}

HRESULT CreateProgram(GLuint* program)
{
	HRESULT hr = S_OK;

	ASSERT(program != nullptr);

	const char* vertexShaderString = "\
		#version 330\n \
		layout(location = 0) in vec4 position; \
		void main() \
		{ \
			gl_Position = position * 2.0f - 1.0f; \
		}";

	const char* pixelShaderString = "\
		#version 330\n \
		out vec4 outputColor; \
		void main() \
		{ \
		   outputColor = vec4(1.0f, 1.0f, 1.0f, 1.0f); \
		} ";

	GLint success;
	GLchar errorsBuf[256];
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	GLuint ps = glCreateShader(GL_FRAGMENT_SHADER);
	GLuint prgm = glCreateProgram();

	glShaderSource(vs, 1, &vertexShaderString, NULL);
	glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success)
	{
		glGetShaderInfoLog(vs, sizeof(errorsBuf), 0, errorsBuf);
		Error("Vertex Shader Errors:\n%s", errorsBuf);
		hr = E_FAIL;
	}

	glShaderSource(ps, 1, &pixelShaderString, NULL);
	glCompileShader(ps);
    glGetShaderiv(ps, GL_COMPILE_STATUS, &success);
    if (!success)
	{
		glGetShaderInfoLog(ps, sizeof(errorsBuf), 0, errorsBuf);
		Error("Pixel Shader Errors:\n%s", errorsBuf);
		hr = E_FAIL;
	}
	
	if (FAILED(hr)) return hr;

	glAttachShader(prgm, vs);
	glAttachShader(prgm, ps);
	glLinkProgram(prgm);
	glGetProgramiv(prgm, GL_LINK_STATUS, &success);
	if (!success)
	{
		glGetProgramInfoLog(prgm, sizeof(errorsBuf), 0, errorsBuf);
		Error("Program Link Errors:\n%s", errorsBuf);
		hr = E_FAIL;
	}

	*program = prgm;

	return hr;
}

HRESULT Init()
{
	HRESULT hr = S_OK;

	// Get OpenGL functions
	glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
	glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
	glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
	glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
	glUseProgram =	  (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
	glAttachShader =  (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
	glLinkProgram =   (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
	glGetProgramiv =  (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
	glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
	glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
	glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
	glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
	glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
	glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
	glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");

	GLuint program;
	IFC( CreateProgram(&program) );
	glUseProgram(program);

	// Calculate random starting positions
	for (uint i = 0; i < g_numVerts; i++)
	{
		g_positions[i].x = rand();
		g_positions[i].y = rand();
	}

	// Initilialize our node list
	for (uint i = 0; i < g_numVerts; i++)
	{
		Node& n = g_nodes[i];
		n.hasChild = false;
		n.hasParent = false;

		// Every node is its own tail initially
		n.tail = i;
	}

	// Initialize buffers
	const uint positionSlot = 0;
    GLsizei stride = sizeof(g_positions[0]);
	GLsizei totalSize = stride * countof(g_positions);
    glGenBuffers(1, &g_vboPos);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboPos);
    glBufferData(GL_ARRAY_BUFFER, totalSize, g_positions, GL_STREAM_DRAW);
    glEnableVertexAttribArray(positionSlot);
    glVertexAttribPointer(positionSlot, 2, GL_FLOAT, GL_FALSE, stride, 0);

Cleanup:
	return hr;
}

HRESULT InitWindow(HWND& hWnd, int width, int height)
{
    LPCSTR wndName = "Flow Snake";

	// Create our window
	WNDCLASSEX wndClass;
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_CLASSDC;
	wndClass.lpfnWndProc = MsgHandler;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = GetModuleHandle(0);
	wndClass.hIcon = NULL;
	wndClass.hCursor = LoadCursor(0, IDC_ARROW);
	wndClass.hbrBackground = NULL;
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = wndName;
	wndClass.hIconSm = NULL;
    RegisterClassEx(&wndClass);
	
    DWORD wndStyle = WS_SYSMENU | WS_CAPTION | WS_VISIBLE | WS_POPUP ;
    DWORD wndExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

	RECT wndRect;
	SetRect(&wndRect, 0, 0, width, height);
    AdjustWindowRectEx(&wndRect, wndStyle, FALSE, wndExStyle);
    int wndWidth = wndRect.right - wndRect.left;
    int wndHeight = wndRect.bottom - wndRect.top;
    int wndTop = GetSystemMetrics(SM_CYSCREEN) / 2 - wndHeight / 2;
    int wndLeft = GetSystemMetrics(SM_XVIRTUALSCREEN) + 
				  GetSystemMetrics(SM_CXSCREEN) / 2 - wndWidth / 2;

	hWnd = CreateWindowEx(0, wndName, wndName, wndStyle, wndLeft, wndTop, 
						  wndWidth, wndHeight, NULL, NULL, NULL, NULL);
	if (hWnd == NULL)
	{
		Error("Window creation failed with error: %x\n", GetLastError());
		return E_FAIL;
	}

	return S_OK;
}

INT WINAPI WinMain(HINSTANCE hInst, HINSTANCE ignoreMe0, LPSTR ignoreMe1, INT ignoreMe2)
{
	HRESULT hr = S_OK;
	HWND hWnd = NULL;
	HDC hDC = NULL;
	HGLRC hRC = NULL;
	MSG msg = {};
	PIXELFORMATDESCRIPTOR pfd;
	
    LARGE_INTEGER previousTime;
    LARGE_INTEGER freqTime;
	double aveDeltaTime = 0.0;

	IFC( InitWindow(hWnd, 1024, 768) );
    hDC = GetDC(hWnd);

    // Create the GL context.
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pixelFormat = ChoosePixelFormat(hDC, &pfd);
	SetPixelFormat(hDC, pixelFormat, &pfd);
    
	hRC = wglCreateContext(hDC);
    wglMakeCurrent(hDC, hRC);

	IFC( Init() );

    QueryPerformanceFrequency(&freqTime);
    QueryPerformanceCounter(&previousTime);
	
    // -------------------
    // Start the Game Loop
    // -------------------
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg); 
            DispatchMessage(&msg);
        }
        else
        {
            LARGE_INTEGER currentTime;
            __int64 elapsed;
            double deltaTime;

            QueryPerformanceCounter(&currentTime);
            elapsed = currentTime.QuadPart - previousTime.QuadPart;
            deltaTime = elapsed * 1000000.0 / freqTime.QuadPart;
			aveDeltaTime = aveDeltaTime * 0.9 + 0.1 * deltaTime;
            previousTime = currentTime;

            IFC( Update((uint) deltaTime) );
			Render();
            SwapBuffers(hDC);
            if (glGetError() != GL_NO_ERROR)
            {
                Error("OpenGL error.\n");
            }
        }
    }

Cleanup:
	if(hRC) wglDeleteContext(hRC);
    //UnregisterClassA(szName, wc.hInstance);

	char strBuf[256];
	sprintf_s(strBuf, "Average frame duration = %.3f ms\n", aveDeltaTime/1000.0f); 
	OutputDebugString(strBuf);

    return FAILED(hr);
}

LRESULT WINAPI MsgHandler(HWND hWnd, uint msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
	case WM_CLOSE:
		PostQuitMessage(0);
		break;

    case WM_KEYDOWN:
        switch (wParam)
        {
            case VK_ESCAPE:
                PostQuitMessage(0);
                break;
        }
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void Error(const char* pStr, ...)
{
    char msg[1024] = {0};
	va_list args;
	va_start(args, pStr);
    _vsnprintf_s(msg, countof(msg), _TRUNCATE, pStr, args);
    OutputDebugString(msg);
}

float rand()
{
	#define RAND_MAX 32767.0f
	static uint seed = 123456789;

	seed = (214013*seed+2531011); 
	return float(((seed>>16)&0x7FFF)/RAND_MAX); 
}