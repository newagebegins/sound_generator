#include <imgui.h>
#include "imgui_impl_glfw_gl3.h"
#include <stdio.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <assert.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

#define PI 3.14159265359f
#define TWOPI (2.0f*PI)
#define REFTIMES_PER_SEC 10000000

IAudioClient *audioClient;
IAudioRenderClient *renderClient;
UINT32 bufferFramesCount;

static const int maxBufferDurationSec = 4;
static const int samplesPerSec = 48000;
static const int maxBufferSize = maxBufferDurationSec * samplesPerSec;

static float signal[maxBufferSize];
static float envelope[maxBufferSize];
static float envelopedSignal[maxBufferSize];

enum WaveType { WAVETYPE_SIN, WAVETYPE_SQUARE, WAVETYPE_SAW, WAVETYPE_TRIANGLE, WAVETYPE_NOISE };
static WaveType waveType = WAVETYPE_SIN;

static float attackSec = 0.1f;
static float decaySec = 0.1f;
static float sustainSec = 0.3f;
static float releaseSec = 0.1f;
static float sustainLevel = 0.5f;
static float frequency = 440.0f;

static int numFrames;

void adjustSound() {
    float duration = attackSec + decaySec + sustainSec + releaseSec;
    numFrames = (int)(duration*samplesPerSec);

    float incr = TWOPI*frequency / samplesPerSec;
    float phase = 0;

    for (int sample = 0; sample < numFrames; ++sample) {
        float val;
        switch (waveType) {
            case WAVETYPE_SIN:
                val = sinf(phase);
                break;
            case WAVETYPE_SQUARE:
                val = (phase <= PI) ? 1.0f : -1.0f;
                break;
            case WAVETYPE_SAW:
                val = 2.0f * (phase / TWOPI) - 1.0f;
                break;
            case WAVETYPE_TRIANGLE:
                val = 2.0f * (phase / TWOPI) - 1.0f;
                if (val < 0) val = -val;
                val = 2.0f * (val - 0.5f);
                break;
            case WAVETYPE_NOISE:
                val = 2.0f*((float)rand() / RAND_MAX) - 1.0f;
                break;
        }
        signal[sample] = val;
        
        phase += incr;
        if (phase >= TWOPI) phase -= TWOPI;
    }

    int attackFrames = (int)(attackSec*samplesPerSec);
    int decayFrames = (int)(decaySec*samplesPerSec);
    int sustainFrames = (int)(sustainSec*samplesPerSec);
    int releaseFrames = (int)(releaseSec*samplesPerSec);

    int sample = 0;
    for (int i = 0; i < attackFrames; ++i, ++sample) {
        envelope[sample] = (float)i / attackFrames;
    }
    for (int i = 0; i < decayFrames; ++i, ++sample) {
        float t = (float)i / decayFrames;
        envelope[sample] = 1.0f + t*(sustainLevel - 1.0f);
    }
    for (int i = 0; i < sustainFrames; ++i, ++sample) {
        envelope[sample] = sustainLevel;
    }
    for (int i = 0; i < releaseFrames; ++i, ++sample) {
        envelope[sample] = sustainLevel - sustainLevel*((float)i / releaseFrames);
    }

    for (int i = 0; i < numFrames; ++i) {
        envelopedSignal[i] = signal[i] * envelope[i];
    }
}

void playSound() {
    HRESULT hr;
    
    hr = audioClient->Stop();
    assert(SUCCEEDED(hr));

    hr = audioClient->Reset();
    assert(SUCCEEDED(hr));

    int16_t *buffer;
    hr = renderClient->GetBuffer(bufferFramesCount, (BYTE**)&buffer);
    assert(SUCCEEDED(hr));

    for (int i = 0; i < numFrames; ++i) {
        buffer[i] = (int16_t)(envelopedSignal[i] * 0x7FFF);
    }
    for (UINT32 i = numFrames; i < bufferFramesCount; ++i) {
        buffer[i] = 0;
    }

    hr = renderClient->ReleaseBuffer(bufferFramesCount, 0);
    assert(SUCCEEDED(hr));

    hr = audioClient->Start();
    assert(SUCCEEDED(hr));
}

void adjustAndPlaySound() {
    adjustSound();
    playSound();
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error %d: %s\n", error, description);
}

int main(int, char**)
{
    // Setup WASAPI
    HRESULT hr;

    IMMDeviceEnumerator *enumerator;
    CoInitialize(NULL);
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&enumerator);
    assert(SUCCEEDED(hr));

    IMMDevice *device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    assert(SUCCEEDED(hr));

    hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&audioClient);
    assert(SUCCEEDED(hr));

    WAVEFORMATEX waveFormat = { 0 };
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = 1;
    waveFormat.nSamplesPerSec = samplesPerSec;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = (waveFormat.nChannels * waveFormat.wBitsPerSample) / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    REFERENCE_TIME duration = maxBufferDurationSec*REFTIMES_PER_SEC;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, duration, 0, &waveFormat, NULL);
    assert(SUCCEEDED(hr));

    hr = audioClient->GetBufferSize(&bufferFramesCount);
    assert(SUCCEEDED(hr));

    hr = audioClient->GetService(IID_IAudioRenderClient, (void**)&renderClient);
    assert(SUCCEEDED(hr));

    // Setup window
    glfwSetErrorCallback(error_callback);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 720, "sound generator", NULL, NULL);
    glfwMakeContextCurrent(window);
    gl3wInit();

    // Setup ImGui binding
    ImGui_ImplGlfwGL3_Init(window, true);

    bool isWindowOpen = true;

    adjustSound();

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplGlfwGL3_NewFrame();

        {
            ImGui::SetNextWindowPos(ImVec2(23, 23), ImGuiSetCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(432, 492), ImGuiSetCond_FirstUseEver);
            ImGui::Begin("sound generator", &isWindowOpen);
            
            if (ImGui::RadioButton("sin", (int*)&waveType, WAVETYPE_SIN)) adjustAndPlaySound();
            ImGui::SameLine();
            if (ImGui::RadioButton("square", (int*)&waveType, WAVETYPE_SQUARE)) adjustAndPlaySound();
            ImGui::SameLine();
            if (ImGui::RadioButton("saw", (int*)&waveType, WAVETYPE_SAW)) adjustAndPlaySound();
            ImGui::SameLine();
            if (ImGui::RadioButton("triangle", (int*)&waveType, WAVETYPE_TRIANGLE)) adjustAndPlaySound();
            ImGui::SameLine();
            if (ImGui::RadioButton("noise", (int*)&waveType, WAVETYPE_NOISE)) adjustAndPlaySound();

            if (ImGui::SliderFloat("attack,s", &attackSec, 0.0f, 1.0f)) adjustAndPlaySound();
            if (ImGui::SliderFloat("decay,s", &decaySec, 0.0f, 1.0f)) adjustAndPlaySound();
            if (ImGui::SliderFloat("sustain,s", &sustainSec, 0.0f, 1.0f)) adjustAndPlaySound();
            if (ImGui::SliderFloat("release,s", &releaseSec, 0.0f, 1.0f)) adjustAndPlaySound();
            if (ImGui::SliderFloat("sustain level", &sustainLevel, 0.0f, 1.0f)) adjustAndPlaySound();
            if (ImGui::SliderFloat("frequency", &frequency, 0.0f, 1000.0f)) adjustAndPlaySound();
            
            ImGui::PlotLines("original signal", signal, numFrames, 0, NULL, FLT_MAX, FLT_MAX, ImVec2(0, 80));
            ImGui::PlotLines("envelope", envelope, numFrames, 0, NULL, FLT_MAX, FLT_MAX, ImVec2(0, 80));
            ImGui::PlotLines("enveloped signal", envelopedSignal, numFrames, 0, NULL, FLT_MAX, FLT_MAX, ImVec2(0, 80));
            
            {
                int numSilentFrames = bufferFramesCount - numFrames;
                UINT32 padding;
                audioClient->GetCurrentPadding(&padding);
                int leftToPlay = padding - numSilentFrames;
                if (leftToPlay < 0) leftToPlay = 0;
                float progress = 1.0f - (float)(leftToPlay) / numFrames;
                ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
            }

            if (ImGui::Button("play")) {
                playSound();
            }

            ImGui::End();
        }

        // Rendering
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplGlfwGL3_Shutdown();
    glfwTerminate();

    return 0;
}
