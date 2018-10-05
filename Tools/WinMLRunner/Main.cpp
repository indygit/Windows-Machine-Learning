#include "Common.h"
#include "OutputHelper.h"
#include "ModelBinding.h"
#include "BindingUtilities.h"
#include "CommandLineArgs.h"
#include <filesystem>

Profiler<WINML_MODEL_TEST_PERF> g_Profiler;

#define RETURN_IF_FAILED(hr) { if (FAILED(hr)) return hr; }

// Binds and evaluates the user-specified model and outputs success/failure for each step. If the
// perf flag is used, it will output the CPU, GPU, and wall-clock time for each step to the
// command-line and to a CSV file.
HRESULT EvaluateModel(LearningModel model, const CommandLineArgs& args, OutputHelper* output, LearningModelDeviceKind deviceKind)
{
    if (model == nullptr)
    {
        return hresult_invalid_argument().code();
    }
    LearningModelSession session = nullptr;

    // Timer measures wall-clock time between the last two start/stop calls.
    Timer timer;

    com_ptr<::IUnknown> spUnkLearningModelDevice;
    UINT adapterIndex = args.GPUAdapterIndex();

    if ((deviceKind != LearningModelDeviceKind::Cpu) && (adapterIndex != -1))
    {
        HRESULT hr = S_OK;

        com_ptr<IDXGIFactory1> dxgiFactory1;
        hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), dxgiFactory1.put_void());
        RETURN_IF_FAILED(hr);

        com_ptr<IDXGIAdapter1> dxgiAdapter1;
        hr = dxgiFactory1->EnumAdapters1(adapterIndex, dxgiAdapter1.put());
        if (FAILED(hr))
        {
            printf("Invalid adapter index : %d\n", adapterIndex);
            return hr;
        }

        DXGI_ADAPTER_DESC1 adapterDesc1;
        dxgiAdapter1->GetDesc1(&adapterDesc1);
        printf("Use adapter : %S\n", adapterDesc1.Description);

        com_ptr<ID3D12Device> d3d12Device;
        hr = D3D12CreateDevice(dxgiAdapter1.get(), D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), d3d12Device.put_void());
        RETURN_IF_FAILED(hr);

        com_ptr<ID3D12CommandQueue> d3d12CommandQueue;
        D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

        hr = d3d12Device->CreateCommandQueue(&commandQueueDesc, __uuidof(ID3D12CommandQueue), d3d12CommandQueue.put_void());
        RETURN_IF_FAILED(hr);

        auto factory = get_activation_factory<LearningModelDevice, ILearningModelDeviceFactoryNative>();

        hr = factory->CreateFromD3D12CommandQueue(d3d12CommandQueue.get(), spUnkLearningModelDevice.put());
        RETURN_IF_FAILED(hr);
    }
    else
    {
        spUnkLearningModelDevice = LearningModelDevice(deviceKind).as<::IUnknown>();
    }

    try
    {
        session =  LearningModelSession(model, spUnkLearningModelDevice.as<LearningModelDevice>());
    }
    catch (hresult_error hr)
    {
        std::cout << "Creating session [FAILED]" << std::endl;
        std::wcout << hr.message().c_str() << std::endl;
        return hr.code();
    }

    if (args.EnableDebugOutput())
    {
        // Enables trace log output. 
        session.EvaluationProperties().Insert(L"EnableDebugOutput", nullptr);
    }

    LearningModelBinding binding(session);

    bool useInputData = false;
    std::string device = deviceKind == LearningModelDeviceKind::Cpu ? "CPU" : "GPU";
    std::cout << "Binding Model on " << device << "...";
    if (args.PerfCapture())
    {
        WINML_PROFILING_START(g_Profiler, WINML_MODEL_TEST_PERF::BIND_VALUE);
        timer.Start();
    }
    if (!args.ImagePath().empty())
    {
        useInputData = true;
        try
        {
            BindingUtilities::BindImageToContext(binding, model, args.ImagePath());
        }
        catch (hresult_error hr)
        {
            std::cout << "[FAILED] Could Not Bind Image To Context" << std::endl;
            std::wcout << hr.message().c_str() << std::endl;
            return hr.code();
        }
    }
    else if (!args.CsvPath().empty())
    {
        useInputData = true;
        try
        {
            BindingUtilities::BindCSVDataToContext(binding, model, args.CsvPath());
        }
        catch (hresult_error hr)
        {
            std::cout << "[FAILED] Could Not Bind CSV Data To Context" << std::endl;
            std::wcout << hr.message().c_str() << std::endl;
            return hr.code();
        }
    }
    else
    {
        try
        {
            BindingUtilities::BindGarbageDataToContext(binding, model);
        }
        catch (hresult_error hr)
        {
            std::cout << "[FAILED] Could Not Garbage Data Context" << std::endl;
            std::wcout << hr.message().c_str() << std::endl;
            return hr.code();
        }
    }
    if (args.PerfCapture())
    {
        WINML_PROFILING_STOP(g_Profiler, WINML_MODEL_TEST_PERF::BIND_VALUE);
        output->m_clockBindTime = timer.Stop();
    }
    std::cout << "[SUCCESS]" << std::endl;

    std::cout << "Evaluating Model on " << device << "...";
    LearningModelEvaluationResult result = nullptr;
    if(args.PerfCapture())
    {
        for (UINT i = 0; i < args.NumIterations(); i++)
        {
            WINML_PROFILING_START(g_Profiler, WINML_MODEL_TEST_PERF::EVAL_MODEL);
            timer.Start();
            try
            {
                result = session.Evaluate(binding, L"");
            }
            catch (hresult_error hr)
            {
                std::cout << "[FAILED]" << std::endl;
                std::wcout << hr.message().c_str() << std::endl;
                return hr.code();
            }
            WINML_PROFILING_STOP(g_Profiler, WINML_MODEL_TEST_PERF::EVAL_MODEL);
            output->m_clockEvalTimes.push_back(timer.Stop());
            std::cout << "[SUCCESS]" << std::endl;
        }

        output->PrintWallClockTimes(args.NumIterations());
        if (deviceKind == LearningModelDeviceKind::Cpu)
        {
            output->PrintCPUTimes(g_Profiler, args.NumIterations());
        }
        else {
            output->PrintGPUTimes(g_Profiler, args.NumIterations());
        }
        g_Profiler.Reset();
    }
    else
    {
        try
        {
            result = session.Evaluate(binding, L"");
        }
        catch (hresult_error hr)
        {
            std::cout << "[FAILED]" << std::endl;
            std::wcout << hr.message().c_str() << std::endl;
            return hr.code();
        }
        std::cout << "[SUCCESS]" << std::endl;
    }

    std::cout << std::endl;

    if (useInputData)
    {
       BindingUtilities::PrintEvaluationResults(model, args, result.Outputs());
    }
    return S_OK;
}

LearningModel LoadModelHelper(const CommandLineArgs& args, OutputHelper * output)
{
    Timer timer;
    LearningModel model = nullptr;

    try
    {
        if (args.PerfCapture())
        {
            WINML_PROFILING_START(g_Profiler, WINML_MODEL_TEST_PERF::LOAD_MODEL);
            timer.Start();
        }
        model = LearningModel::LoadFromFilePath(args.ModelPath());
    }
    catch (hresult_error hr)
    {
        std::wcout << "Load Model: " << args.ModelPath() << " [FAILED]" << std::endl;
        std::wcout << hr.message().c_str() << std::endl;
        throw;
    }
    if (args.PerfCapture())
    {
        WINML_PROFILING_STOP(g_Profiler, WINML_MODEL_TEST_PERF::LOAD_MODEL);
        output->m_clockLoadTime = timer.Stop();
    }
    output->PrintModelInfo(args.ModelPath(), model);
    std::cout << "Loading model...[SUCCESS]" << std::endl;

    return model;
}

HRESULT EvaluateModelsInDirectory(CommandLineArgs& args, OutputHelper * output)
{
    std::wstring folderPath = args.FolderPath();
    for (auto & it : std::filesystem::directory_iterator(args.FolderPath()))
    {
        std::string path = it.path().string();
        if (it.path().string().find(".onnx") != std::string::npos ||
            it.path().string().find(".pb") != std::string::npos)
        {
            std::wstring fileName;
            fileName.assign(path.begin(), path.end());
            args.SetModelPath(fileName);
            LearningModel model = nullptr;
            try
            {
                model = LoadModelHelper(args, output);
            }
            catch (hresult_error hr)
            {
                std::cout << hr.message().c_str() << std::endl;
                return hr.code();
            }
            if (args.UseCPUandGPU() || args.UseCPU())
            {
                HRESULT evalHResult = EvaluateModel(model, args, output, LearningModelDeviceKind::Cpu);
                if (evalHResult != S_OK)
                {
                    return evalHResult;
                }
            }
            if (args.UseCPUandGPU() || args.UseGPU())
            {
                HRESULT evalHResult = EvaluateModel(model, args, output, args.DeviceKind());
                if (evalHResult != S_OK)
                {
                    return evalHResult;
                }
            }
            output->WritePerformanceDataToCSV(g_Profiler, args, fileName);
            output->Reset();
        }
    }
    return S_OK;
}

int main(int argc, char** argv)
{
    CommandLineArgs args;
    OutputHelper output;

    // Initialize COM in a multi-threaded environment.
    winrt::init_apartment();

    // Profiler is a wrapper class that captures and stores timing and memory usage data on the
    // CPU and GPU.
    g_Profiler.Enable();
    output.SetDefaultCSVFileName();

    if (!args.ModelPath().empty())
    {
        output.PrintHardwareInfo();
        LearningModel model = nullptr;
        try
        {
            model = LoadModelHelper(args, &output);
        }
        catch (hresult_error hr)
        {
            std::cout << hr.message().c_str() << std::endl;
            return hr.code();
        }
        if (args.UseCPUandGPU() || args.UseCPU())
        {
            HRESULT evalHResult = EvaluateModel(model, args, &output, LearningModelDeviceKind::Cpu);
            if (FAILED(evalHResult))
            {
                return evalHResult;
            }
        }
        if (args.UseCPUandGPU() || args.UseGPU())
        {
            HRESULT evalHResult = EvaluateModel(model, args, &output, args.DeviceKind());
            if (FAILED(evalHResult))
            {
                return evalHResult;
            }
        }
        output.WritePerformanceDataToCSV(g_Profiler, args, args.ModelPath());
        output.Reset();
    }
    else if (!args.FolderPath().empty())
    {
        output.PrintHardwareInfo();
        return EvaluateModelsInDirectory(args, &output);
    }
    return 0;
}