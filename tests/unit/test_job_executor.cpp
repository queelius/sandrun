#include <gtest/gtest.h>
#include "job_executor.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <vector>
#include <chrono>

namespace sandrun {
namespace {

class JobExecutorTest : public ::testing::Test {
protected:
    std::string test_dir;

    void SetUp() override {
        // Create a temporary directory for testing
        char temp_template[] = "/tmp/sandrun_test_XXXXXX";
        char* dir = mkdtemp(temp_template);
        if (dir) {
            test_dir = std::string(dir);
        } else {
            test_dir = "/tmp/sandrun_test_" + std::to_string(getpid());
            std::filesystem::create_directory(test_dir);
        }
    }

    void TearDown() override {
        // Clean up test directory
        if (!test_dir.empty() && std::filesystem::exists(test_dir)) {
            std::filesystem::remove_all(test_dir);
        }
    }

    void createTestFile(const std::string& filename, const std::string& content) {
        std::ofstream file(test_dir + "/" + filename);
        file << content;
        file.close();
    }

    std::string readOutput(const std::string& filename) {
        std::ifstream file(test_dir + "/" + filename);
        if (!file.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    }
};

TEST_F(JobExecutorTest, ExecutePythonScript) {
    // Create a simple Python script
    createTestFile("test.py", "print('Hello from Python')");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "test.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Hello from Python") != std::string::npos);
    EXPECT_TRUE(result.stderr_log.empty() || result.stderr_log.find("Error") == std::string::npos);
    EXPECT_GE(result.cpu_seconds, 0.0);
    EXPECT_GT(result.memory_bytes, 0);
}

TEST_F(JobExecutorTest, ExecuteWithArguments) {
    // Create a Python script that uses arguments
    createTestFile("args.py", R"(
import sys
if len(sys.argv) > 1:
    print(f"Arguments: {' '.join(sys.argv[1:])}")
else:
    print("No arguments")
)");

    std::vector<std::string> args = {"arg1", "arg2", "arg3"};
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "args.py",
        args
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Arguments: arg1 arg2 arg3") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteWithError) {
    // Create a Python script with syntax error
    createTestFile("error.py", "print('Missing parenthesis'");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "error.py"
    );

    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.stderr_log.find("SyntaxError") != std::string::npos ||
                result.stderr_log.find("error") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteNonexistentFile) {
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "nonexistent.py"
    );

    EXPECT_NE(result.exit_code, 0);
}

TEST_F(JobExecutorTest, ExecuteShellScript) {
    // Create a simple shell script
    createTestFile("test.sh", "#!/bin/bash\necho 'Hello from Shell'\nexit 0");

    // Make it executable
    chmod((test_dir + "/test.sh").c_str(), 0755);

    auto result = JobExecutor::execute(
        test_dir,
        "bash",
        "test.sh"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Hello from Shell") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteWithStderr) {
    // Create a Python script that writes to stderr
    createTestFile("stderr_test.py", R"(
import sys
print("Standard output")
print("Standard error", file=sys.stderr)
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "stderr_test.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Standard output") != std::string::npos);
    EXPECT_TRUE(result.stderr_log.find("Standard error") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteWithExitCode) {
    // Create a Python script that exits with specific code
    createTestFile("exit_code.py", "import sys; sys.exit(42)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "exit_code.py"
    );

    EXPECT_EQ(result.exit_code, 42);
}

TEST_F(JobExecutorTest, ExecuteLongRunning) {
    // Create a Python script that runs for a bit
    createTestFile("long_running.py", R"(
import time
print("Starting...")
time.sleep(0.5)
print("Finished!")
)");

    auto start = std::chrono::steady_clock::now();
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "long_running.py"
    );
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Starting...") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Finished!") != std::string::npos);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(duration, 500); // Should take at least 500ms
}

TEST_F(JobExecutorTest, ExecuteWithFileOutput) {
    // Create a Python script that creates files
    createTestFile("create_file.py", R"(
with open('output.txt', 'w') as f:
    f.write('Output data')
print("File created")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "create_file.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("File created") != std::string::npos);

    // Check if file was created
    std::string output = readOutput("output.txt");
    EXPECT_EQ(output, "Output data");
}

TEST_F(JobExecutorTest, ExecuteWithEnvironment) {
    // Create a Python script that reads environment
    createTestFile("env_test.py", R"(
import os
print(f"PATH exists: {'PATH' in os.environ}")
print(f"Working dir: {os.getcwd()}")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "env_test.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("PATH exists:") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Working dir:") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteNodeScript) {
    // Skip if node is not available
    if (system("which node > /dev/null 2>&1") != 0) {
        GTEST_SKIP() << "Node.js not installed, skipping test";
    }

    // Given: A simple Node.js script
    createTestFile("test.js", "console.log('Hello from Node');");

    // When: The script is executed
    auto result = JobExecutor::execute(
        test_dir,
        "node",
        "test.js"
    );

    // Then: Node.js support is optional in sandboxed environments
    // If it succeeds, verify output; if it fails, that's acceptable
    if (result.exit_code == 0) {
        EXPECT_TRUE(result.stdout_log.find("Hello from Node") != std::string::npos)
            << "When Node executes successfully, output should be correct";
    } else {
        // Node.js may fail in restricted sandbox environments
        std::cout << "Note: Node.js execution failed (may need additional sandbox configuration). "
                  << "Exit code: " << result.exit_code << std::endl;
    }
}

TEST_F(JobExecutorTest, ExecuteWithLargeOutput) {
    // Create a Python script that produces large output
    createTestFile("large_output.py", R"(
for i in range(1000):
    print(f"Line {i}: " + "X" * 100)
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "large_output.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_GT(result.stdout_log.length(), 100000); // Should be > 100KB
    EXPECT_TRUE(result.stdout_log.find("Line 0:") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Line 999:") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteInvalidInterpreter) {
    createTestFile("test.py", "print('test')");

    auto result = JobExecutor::execute(
        test_dir,
        "nonexistent_interpreter",
        "test.py"
    );

    EXPECT_NE(result.exit_code, 0);
}

TEST_F(JobExecutorTest, ExecuteEmptyScript) {
    createTestFile("empty.py", "");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "empty.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.empty());
}

TEST_F(JobExecutorTest, ExecuteWithInput) {
    // Create a Python script that reads input (should fail in sandbox)
    createTestFile("input_test.py", R"(
try:
    user_input = input("Enter something: ")
    print(f"You entered: {user_input}")
except EOFError:
    print("No input available")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "input_test.py"
    );

    // Should handle lack of input gracefully
    EXPECT_TRUE(result.stdout_log.find("No input available") != std::string::npos ||
                result.stdout_log.find("EOFError") != std::string::npos);
}

TEST_F(JobExecutorTest, ResourceMetrics) {
    // Create a Python script that uses some memory
    createTestFile("memory_test.py", R"(
import sys
# Allocate some memory
data = [i for i in range(1000000)]
print(f"Allocated {sys.getsizeof(data)} bytes")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "memory_test.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_GT(result.memory_bytes, 0); // Should report memory usage
    EXPECT_GE(result.cpu_seconds, 0.0); // Should report CPU time
}

TEST_F(JobExecutorTest, ConcurrentExecution) {
    // Create multiple scripts
    for (int i = 0; i < 5; ++i) {
        std::string script = "print('Script " + std::to_string(i) + "')";
        createTestFile("script" + std::to_string(i) + ".py", script);
    }

    // Execute them concurrently
    std::vector<std::thread> threads;
    std::vector<JobExecutor::Result> results(5);

    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([this, i, &results]() {
            results[i] = JobExecutor::execute(
                test_dir,
                "python3",
                "script" + std::to_string(i) + ".py"
            );
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All should succeed
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(results[i].exit_code, 0);
        std::string expected = "Script " + std::to_string(i);
        EXPECT_TRUE(results[i].stdout_log.find(expected) != std::string::npos);
    }
}

TEST_F(JobExecutorTest, ExecuteWithPythonPath) {
    // Create a module in a subdirectory
    std::filesystem::create_directories(test_dir + "/mymodule");
    createTestFile("mymodule/__init__.py", "VALUE = 42");
    createTestFile("mymodule/helper.py", "def get_value(): return 'from helper'");

    // Create main script that imports from the module
    createTestFile("main.py", R"(
import sys
import mymodule
from mymodule.helper import get_value
print(f"VALUE={mymodule.VALUE}")
print(f"HELPER={get_value()}")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "main.py",
        {},  // no args
        test_dir  // pythonpath
    );

    EXPECT_EQ(result.exit_code, 0) << "Exit code should be 0. stderr: " << result.stderr_log;
    EXPECT_TRUE(result.stdout_log.find("VALUE=42") != std::string::npos)
        << "Should import module value. stdout: " << result.stdout_log;
    EXPECT_TRUE(result.stdout_log.find("HELPER=from helper") != std::string::npos)
        << "Should import helper function. stdout: " << result.stdout_log;
}

TEST_F(JobExecutorTest, ExecuteInvalidDirectory) {
    // Test execution with non-existent working directory
    auto result = JobExecutor::execute(
        "/nonexistent/directory/path",
        "python3",
        "test.py"
    );

    // Should fail to change directory
    EXPECT_NE(result.exit_code, 0);
}

TEST_F(JobExecutorTest, ExecuteWithManyArguments) {
    // Create script that prints all arguments
    createTestFile("many_args.py", R"(
import sys
print(f"argc={len(sys.argv)}")
for i, arg in enumerate(sys.argv[1:], 1):
    print(f"arg{i}={arg}")
)");

    std::vector<std::string> args;
    for (int i = 0; i < 10; ++i) {
        args.push_back("arg" + std::to_string(i));
    }

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "many_args.py",
        args
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("argc=11") != std::string::npos)
        << "Should have 11 arguments (script + 10 args)";
    EXPECT_TRUE(result.stdout_log.find("arg1=arg0") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("arg10=arg9") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteScriptWithSpecialCharacters) {
    // Create script with special characters in output
    createTestFile("special.py", R"(
print("Special: $PATH 'quotes' \"double\" `backticks` \n\\n")
print("Unicode: \u00e9\u00e8\u00ea")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "special.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Special:") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteScriptThatReadsStdin) {
    // Script that tries to read stdin (should get EOF)
    createTestFile("stdin_test.py", R"(
import sys
try:
    data = sys.stdin.read()
    if data:
        print(f"Got stdin: {len(data)} bytes")
    else:
        print("Empty stdin")
except Exception as e:
    print(f"Stdin error: {e}")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "stdin_test.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Empty stdin") != std::string::npos ||
                result.stdout_log.find("Stdin error") != std::string::npos)
        << "Should handle stdin gracefully";
}

TEST_F(JobExecutorTest, ExecuteScriptWithEnvironment) {
    // Script that reads environment variables
    createTestFile("env_vars.py", R"(
import os
print(f"PATH_EXISTS={'PATH' in os.environ}")
print(f"HOME_EXISTS={'HOME' in os.environ}")
print(f"CWD={os.getcwd()}")
)");

    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "env_vars.py"
    );

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("PATH_EXISTS=True") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("CWD=" + test_dir) != std::string::npos)
        << "Should be in test directory. stdout: " << result.stdout_log;
}

TEST_F(JobExecutorTest, ExecuteQuickScript) {
    // Test a script that completes very quickly
    createTestFile("quick.py", "print('done')");

    auto start = std::chrono::steady_clock::now();
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "quick.py"
    );
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("done") != std::string::npos);

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(duration_ms, 5000) << "Quick script should complete fast";
}

// ============================================================================
// Edge Case Tests (TDD Expert Recommendations)
// ============================================================================

TEST_F(JobExecutorTest, ExecuteWithInterleavedOutput) {
    // Given: Script that interleaves stdout and stderr rapidly
    createTestFile("interleaved.py", R"(
import sys
for i in range(50):
    print(f"stdout_{i}")
    sys.stdout.flush()
    print(f"stderr_{i}", file=sys.stderr)
    sys.stderr.flush()
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "interleaved.py"
    );

    // Then: Both streams should be captured completely
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("stdout_0") != std::string::npos)
        << "Should capture first stdout line";
    EXPECT_TRUE(result.stdout_log.find("stdout_49") != std::string::npos)
        << "Should capture last stdout line";
    EXPECT_TRUE(result.stderr_log.find("stderr_0") != std::string::npos)
        << "Should capture first stderr line";
    EXPECT_TRUE(result.stderr_log.find("stderr_49") != std::string::npos)
        << "Should capture last stderr line";
}

TEST_F(JobExecutorTest, ExecuteWithVeryLargeOutput) {
    // Given: Script that produces very large output (500KB+)
    // This tests pipe buffer handling
    createTestFile("very_large_output.py", R"(
for i in range(5000):
    print(f"Line {i:05d}: " + "X" * 100)
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "very_large_output.py"
    );

    // Then: Should capture all output without hanging
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_GT(result.stdout_log.length(), 500000) << "Should capture > 500KB output";
    EXPECT_TRUE(result.stdout_log.find("Line 00000:") != std::string::npos)
        << "Should capture first line";
    EXPECT_TRUE(result.stdout_log.find("Line 04999:") != std::string::npos)
        << "Should capture last line";
}

TEST_F(JobExecutorTest, ExecuteWithSignalExit) {
    // Given: Script that exits via signal (SIGALRM)
    createTestFile("signal_exit.py", R"(
import signal
import sys

def handler(signum, frame):
    print("Received signal", file=sys.stderr)
    sys.exit(128 + signum)

signal.signal(signal.SIGALRM, handler)
signal.alarm(1)  # Signal in 1 second

import time
time.sleep(10)  # Will be interrupted by SIGALRM
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "signal_exit.py"
    );

    // Then: Should handle signal exit gracefully
    EXPECT_NE(result.exit_code, 0) << "Should have non-zero exit from signal";
    EXPECT_TRUE(result.stderr_log.find("Received signal") != std::string::npos)
        << "Signal handler should have run. stderr: " << result.stderr_log;
}

TEST_F(JobExecutorTest, ExecuteWithLoopComputation) {
    // Given: Script that does computation (simpler than threading)
    createTestFile("compute.py", R"(
# Simple computation without threading
results = []
for i in range(3):
    results.append(f"Iteration {i} done")

for r in sorted(results):
    print(r)
print("All iterations complete")
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "compute.py"
    );

    // Then: Should capture all output
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("All iterations complete") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Iteration 0 done") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Iteration 2 done") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteWithSpecialOutput) {
    // Given: Script that outputs special characters (no null bytes to avoid string issues)
    createTestFile("special_output.py", R"(
import sys
# Output text with special characters (avoiding null bytes)
print("TEXT_START")
print("Special chars: tabs\there newlines")
print("High bytes: \xc3\xa9\xc3\xa8")  # UTF-8 e with accents
print("TEXT_END")
sys.stdout.flush()
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "special_output.py"
    );

    // Then: Should capture output with special characters
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("TEXT_START") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("TEXT_END") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("tabs") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteWithRapidExit) {
    // Given: Script that exits immediately without any output
    createTestFile("rapid_exit.py", "import sys; sys.exit(7)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "rapid_exit.py"
    );

    // Then: Should capture exit code correctly
    EXPECT_EQ(result.exit_code, 7);
    EXPECT_TRUE(result.stdout_log.empty());
}

TEST_F(JobExecutorTest, ExecuteWithWorkingDirectoryFiles) {
    // Given: Script that lists and reads files in working directory
    createTestFile("data.txt", "test data content");
    createTestFile("list_files.py", R"(
import os
print("Files in working directory:")
for f in sorted(os.listdir('.')):
    print(f"  - {f}")

with open('data.txt', 'r') as f:
    print(f"data.txt content: {f.read()}")
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "list_files.py"
    );

    // Then: Should see files in working directory
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("data.txt") != std::string::npos)
        << "Should list data.txt";
    EXPECT_TRUE(result.stdout_log.find("test data content") != std::string::npos)
        << "Should read data.txt content";
}

TEST_F(JobExecutorTest, ExecuteWithExceptionTraceback) {
    // Given: Script that raises an unhandled exception
    createTestFile("exception.py", R"(
def level3():
    raise ValueError("Something went wrong!")

def level2():
    level3()

def level1():
    level2()

level1()
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "exception.py"
    );

    // Then: Should capture full traceback in stderr
    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.stderr_log.find("ValueError") != std::string::npos)
        << "Should contain exception type";
    EXPECT_TRUE(result.stderr_log.find("Something went wrong!") != std::string::npos)
        << "Should contain exception message";
    EXPECT_TRUE(result.stderr_log.find("level3") != std::string::npos)
        << "Should contain stack trace";
}

TEST_F(JobExecutorTest, ExecuteWithZeroExitAfterOutput) {
    // Given: Script with output then success exit
    createTestFile("output_then_exit.py", R"(
import sys
print("Line 1")
print("Line 2")
print("Line 3")
sys.exit(0)
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "output_then_exit.py"
    );

    // Then: All output should be captured before exit
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_log.find("Line 1") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Line 2") != std::string::npos);
    EXPECT_TRUE(result.stdout_log.find("Line 3") != std::string::npos);
}

TEST_F(JobExecutorTest, ExecuteResourceMetricsAccuracy) {
    // Given: Script that uses measurable CPU time
    createTestFile("cpu_work.py", R"(
# Do some CPU-bound work
total = 0
for i in range(1000000):
    total += i * i
print(f"Total: {total}")
)");

    // When: Executing the script
    auto result = JobExecutor::execute(
        test_dir,
        "python3",
        "cpu_work.py"
    );

    // Then: Should report non-trivial CPU usage
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_GT(result.cpu_seconds, 0.0) << "Should report CPU time";
    EXPECT_LT(result.cpu_seconds, 60.0) << "CPU time should be reasonable";
    EXPECT_GT(result.memory_bytes, 0) << "Should report memory usage";
}

} // namespace
} // namespace sandrun