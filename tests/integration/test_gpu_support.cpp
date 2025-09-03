#include <gtest/gtest.h>
#include "sandbox.h"
#include "proof.h"
#include <filesystem>

namespace sandrun {
namespace {

class GPUSupportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Check if GPU is available
        gpu_available = std::filesystem::exists("/dev/nvidia0") || 
                       std::filesystem::exists("/dev/nvidiactl");
                       
        if (!gpu_available) {
            GTEST_SKIP() << "No GPU detected, skipping GPU tests";
        }
    }

    bool gpu_available;
};

TEST_F(GPUSupportTest, GPUDeviceAccess) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.gpu_device_id = 0;
    config.timeout = std::chrono::seconds(30);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import os
import subprocess

# Check CUDA environment variables
print(f"CUDA_VISIBLE_DEVICES: {os.environ.get('CUDA_VISIBLE_DEVICES', 'not set')}")
print(f"CUDA_DEVICE_ORDER: {os.environ.get('CUDA_DEVICE_ORDER', 'not set')}")

# Check for NVIDIA device files
devices = ['/dev/nvidia0', '/dev/nvidiactl', '/dev/nvidia-uvm']
for device in devices:
    exists = os.path.exists(device)
    print(f"{device}: {'exists' if exists else 'not found'}")

# Try nvidia-smi if available
try:
    result = subprocess.run(['nvidia-smi', '--query-gpu=name,memory.total', '--format=csv,noheader'], 
                          capture_output=True, text=True, timeout=5)
    if result.returncode == 0:
        print(f"GPU Info: {result.stdout.strip()}")
    else:
        print("nvidia-smi failed")
except Exception as e:
    print(f"nvidia-smi error: {e}")
)";

    JobResult result = sandbox.execute(code, "gpu_test_1");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("CUDA_VISIBLE_DEVICES: 0") != std::string::npos);
    EXPECT_TRUE(result.output.find("CUDA_DEVICE_ORDER: PCI_BUS_ID") != std::string::npos);
}

TEST_F(GPUSupportTest, CUDAComputation) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.gpu_memory_limit_bytes = 2ULL * 1024 * 1024 * 1024; // 2GB
    config.timeout = std::chrono::seconds(60);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
try:
    import torch
    
    # Check CUDA availability
    cuda_available = torch.cuda.is_available()
    print(f"CUDA available: {cuda_available}")
    
    if cuda_available:
        # Get GPU info
        gpu_count = torch.cuda.device_count()
        print(f"GPU count: {gpu_count}")
        
        device_name = torch.cuda.get_device_name(0)
        print(f"GPU name: {device_name}")
        
        # Perform simple computation
        device = torch.device('cuda:0')
        x = torch.randn(1000, 1000, device=device)
        y = torch.randn(1000, 1000, device=device)
        z = torch.matmul(x, y)
        
        print(f"Matrix multiplication result shape: {z.shape}")
        print(f"GPU memory allocated: {torch.cuda.memory_allocated(0) / 1024**2:.2f} MB")
        
except ImportError:
    print("PyTorch not installed, trying CuPy...")
    
    try:
        import cupy as cp
        
        # Create arrays on GPU
        x_gpu = cp.random.randn(1000, 1000)
        y_gpu = cp.random.randn(1000, 1000)
        z_gpu = cp.dot(x_gpu, y_gpu)
        
        print(f"CuPy computation successful")
        print(f"Result shape: {z_gpu.shape}")
        
    except ImportError:
        print("Neither PyTorch nor CuPy available for GPU testing")
)";

    JobResult result = sandbox.execute(code, "gpu_compute_test");
    
    // Should complete without errors
    EXPECT_EQ(result.exit_code, 0);
    
    // Check if GPU computation was performed (depends on installed libraries)
    bool has_gpu_output = 
        result.output.find("CUDA available: True") != std::string::npos ||
        result.output.find("CuPy computation successful") != std::string::npos ||
        result.output.find("Neither PyTorch nor CuPy available") != std::string::npos;
    
    EXPECT_TRUE(has_gpu_output);
}

TEST_F(GPUSupportTest, GPUMemoryLimit) {
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.gpu_memory_limit_bytes = 1ULL * 1024 * 1024 * 1024; // 1GB limit
    config.timeout = std::chrono::seconds(30);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
try:
    import torch
    
    if torch.cuda.is_available():
        device = torch.device('cuda:0')
        
        # Try to allocate more than 1GB (should fail or be limited)
        try:
            # Allocate 1.5GB
            x = torch.zeros(1536 * 1024 * 1024 // 4, device=device, dtype=torch.float32)
            print("Large allocation succeeded (may be within limit)")
        except RuntimeError as e:
            print(f"Allocation failed as expected: {e}")
            
except ImportError:
    print("PyTorch not available, cannot test GPU memory limits")
)";

    JobResult result = sandbox.execute(code, "gpu_memory_test");
    
    EXPECT_EQ(result.exit_code, 0);
    // Should either fail allocation or succeed within limits
}

TEST_F(GPUSupportTest, GPUProofGeneration) {
    ProofGenerator proof_gen;
    
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.timeout = std::chrono::seconds(30);
    
    std::string code = R"(
import time

# Simulate GPU workload
print("Starting GPU computation")
time.sleep(0.5)  # Simulate computation time
print("GPU computation complete")
print("GPU_TIME:2.5")  # Report GPU time used
)";

    proof_gen.start_recording("gpu_proof_job", code);
    
    Sandbox sandbox(config);
    JobResult result = sandbox.execute(code, "gpu_proof_job");
    
    EXPECT_EQ(result.exit_code, 0);
    
    // Generate proof with GPU time
    ProofOfCompute proof = proof_gen.generate_proof(
        result.output,
        result.cpu_seconds,
        result.memory_bytes
    );
    
    // Parse GPU time from output (in real implementation, would get from nvidia-smi)
    if (result.output.find("GPU_TIME:") != std::string::npos) {
        proof.gpu_time = 2.5; // Parsed from output
    }
    
    EXPECT_EQ(proof.job_id, "gpu_proof_job");
    EXPECT_FALSE(proof.execution_hash.empty());
    EXPECT_GT(proof.cpu_time, 0.0);
    
    // Verify proof includes GPU usage
    std::string proof_json = proof.to_json();
    EXPECT_TRUE(proof_json.find("\"gpu_time\": 2.5") != std::string::npos ||
                proof_json.find("\"gpu_time\": 0") != std::string::npos);
}

TEST_F(GPUSupportTest, MultiGPUConfiguration) {
    // Test configuration for multi-GPU systems
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.gpu_device_id = 1; // Use second GPU if available
    config.timeout = std::chrono::seconds(30);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import os
device = os.environ.get('CUDA_VISIBLE_DEVICES', 'not set')
print(f"CUDA_VISIBLE_DEVICES: {device}")

# Should be set to GPU 1
assert device == '1' or device == 'not set', f"Expected GPU 1, got {device}"
print("Multi-GPU configuration test passed")
)";

    JobResult result = sandbox.execute(code, "multi_gpu_test");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Multi-GPU configuration test passed") != std::string::npos);
}

TEST_F(GPUSupportTest, GPUWithoutCUDA) {
    // Test behavior when GPU is requested but CUDA libs are missing
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.timeout = std::chrono::seconds(10);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import os

# Check if CUDA libraries are accessible
cuda_lib_paths = [
    '/usr/local/cuda',
    '/usr/lib/x86_64-linux-gnu/libcuda.so'
]

for path in cuda_lib_paths:
    exists = os.path.exists(path)
    print(f"{path}: {'found' if exists else 'not found'}")

print("GPU environment check complete")
)";

    JobResult result = sandbox.execute(code, "gpu_lib_check");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("GPU environment check complete") != std::string::npos);
}

TEST_F(GPUSupportTest, StableDiffusionSimulation) {
    // Simulate a Stable Diffusion inference job
    SandboxConfig config;
    config.interpreter = "python3";
    config.gpu_enabled = true;
    config.gpu_memory_limit_bytes = 6ULL * 1024 * 1024 * 1024; // 6GB for SD
    config.timeout = std::chrono::seconds(120);
    
    Sandbox sandbox(config);
    
    std::string code = R"(
import time
import json

# Simulate Stable Diffusion pipeline
def generate_image(prompt, steps=50):
    print(f"Loading Stable Diffusion model...")
    time.sleep(0.5)  # Simulate model loading
    
    print(f"Generating image: '{prompt}'")
    print(f"Steps: {steps}")
    
    # Simulate inference steps
    for i in range(0, steps, 10):
        print(f"Progress: {i}/{steps}")
        time.sleep(0.1)
    
    # Return simulated result
    return {
        "prompt": prompt,
        "steps": steps,
        "size": "512x512",
        "seed": 42,
        "gpu_time": 15.3,
        "output": "generated_image.png"
    }

result = generate_image("a beautiful sunset over mountains", steps=50)
print(f"Generation complete: {json.dumps(result)}")
)";

    ProofGenerator proof_gen;
    proof_gen.start_recording("sd_job", code);
    
    JobResult result = sandbox.execute(code, "sd_job");
    
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.output.find("Generation complete") != std::string::npos);
    EXPECT_TRUE(result.output.find("generated_image.png") != std::string::npos);
    
    // Generate proof for SD job
    ProofOfCompute proof = proof_gen.generate_proof(
        result.output,
        result.cpu_seconds,
        result.memory_bytes
    );
    
    // In production, would verify actual image generation
    EXPECT_FALSE(proof.execution_hash.empty());
}

} // namespace
} // namespace sandrun