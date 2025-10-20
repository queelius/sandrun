#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <memory>

namespace sandrun {

// Environment template configuration
struct EnvironmentTemplate {
    std::string name;                      // e.g., "ml-basic", "vision"
    std::string base_image;                // Base Python version (e.g., "python:3.11")
    std::vector<std::string> packages;     // pip packages to install
    std::string setup_script;              // Optional setup script path
    int max_age_hours = 24;                // Max age before eviction
    bool gpu_enabled = false;              // Whether this env needs GPU
};

// Cached environment instance
struct CachedEnvironment {
    std::string template_name;
    std::string base_path;                 // Path to cached environment
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_used;
    size_t use_count = 0;
    bool ready = false;                    // Setup completed successfully
};

// Environment manager - handles template creation, caching, and cleanup
class EnvironmentManager {
public:
    static EnvironmentManager& instance() {
        static EnvironmentManager instance;
        return instance;
    }

    // Register an environment template
    void register_template(const EnvironmentTemplate& tmpl);

    // Get or create environment from template
    // Returns working directory path for job (copy-on-write clone)
    std::string prepare_environment(
        const std::string& template_name,
        const std::string& job_id
    );

    // Check if template exists
    bool has_template(const std::string& name) const;

    // Get list of available templates
    std::vector<std::string> list_templates() const;

    // Cleanup old/unused environments
    void cleanup_old_environments();

    // Force rebuild of template
    void rebuild_template(const std::string& template_name);

    // Get statistics
    struct Stats {
        int total_templates;
        int cached_environments;
        int total_uses;
        size_t disk_usage_mb;
    };
    Stats get_stats() const;

private:
    EnvironmentManager();
    ~EnvironmentManager() = default;

    // Build base environment from template
    std::string build_base_environment(const EnvironmentTemplate& tmpl);

    // Clone environment using copy-on-write (overlayfs or cp -r)
    std::string clone_environment(
        const std::string& base_path,
        const std::string& job_id
    );

    // Install Python packages in environment
    bool install_packages(
        const std::string& env_path,
        const std::vector<std::string>& packages
    );

    // Run setup script in environment
    bool run_setup_script(
        const std::string& env_path,
        const std::string& script_path
    );

    // Calculate directory size
    size_t get_directory_size(const std::string& path) const;

    mutable std::mutex mutex_;
    std::map<std::string, EnvironmentTemplate> templates_;
    std::map<std::string, CachedEnvironment> cached_envs_;
    std::string cache_base_dir_ = "/tmp/sandrun_envs";
};

// Built-in environment templates
namespace BuiltInTemplates {
    // Basic ML environment (NumPy, Pandas, Scikit-learn)
    EnvironmentTemplate ml_basic();

    // Computer vision (PyTorch, Torchvision, OpenCV, Pillow)
    EnvironmentTemplate vision();

    // NLP/Transformers (PyTorch, Transformers, Tokenizers)
    EnvironmentTemplate nlp();

    // Data science (Pandas, NumPy, Matplotlib, Seaborn, Jupyter)
    EnvironmentTemplate data_science();

    // Scientific computing (NumPy, SciPy, SymPy, Matplotlib)
    EnvironmentTemplate scientific();
}

} // namespace sandrun
