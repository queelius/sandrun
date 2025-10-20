#include "environment_manager.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace sandrun {

EnvironmentManager::EnvironmentManager() {
    // Create base cache directory
    fs::create_directories(cache_base_dir_);

    // Register built-in templates
    register_template(BuiltInTemplates::ml_basic());
    register_template(BuiltInTemplates::vision());
    register_template(BuiltInTemplates::nlp());
    register_template(BuiltInTemplates::data_science());
    register_template(BuiltInTemplates::scientific());

    std::cout << "[EnvManager] Initialized with " << templates_.size()
              << " built-in templates" << std::endl;
}

void EnvironmentManager::register_template(const EnvironmentTemplate& tmpl) {
    std::lock_guard<std::mutex> lock(mutex_);
    templates_[tmpl.name] = tmpl;
    std::cout << "[EnvManager] Registered template: " << tmpl.name << std::endl;
}

bool EnvironmentManager::has_template(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return templates_.count(name) > 0;
}

std::vector<std::string> EnvironmentManager::list_templates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : templates_) {
        names.push_back(name);
    }
    return names;
}

std::string EnvironmentManager::prepare_environment(
    const std::string& template_name,
    const std::string& job_id
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if template exists
    auto tmpl_it = templates_.find(template_name);
    if (tmpl_it == templates_.end()) {
        throw std::runtime_error("Environment template not found: " + template_name);
    }

    const auto& tmpl = tmpl_it->second;

    // Check if we have a cached base environment
    auto cache_it = cached_envs_.find(template_name);

    std::string base_path;
    if (cache_it != cached_envs_.end() && cache_it->second.ready) {
        // Use cached environment
        base_path = cache_it->second.base_path;
        cache_it->second.last_used = std::chrono::steady_clock::now();
        cache_it->second.use_count++;

        std::cout << "[EnvManager] Using cached environment: " << template_name
                  << " (used " << cache_it->second.use_count << " times)" << std::endl;
    } else {
        // Build new base environment
        std::cout << "[EnvManager] Building new environment: " << template_name << std::endl;
        base_path = build_base_environment(tmpl);

        // Cache it
        CachedEnvironment cached;
        cached.template_name = template_name;
        cached.base_path = base_path;
        cached.created_at = std::chrono::steady_clock::now();
        cached.last_used = cached.created_at;
        cached.use_count = 1;
        cached.ready = true;

        cached_envs_[template_name] = cached;
    }

    // Clone environment for this specific job
    return clone_environment(base_path, job_id);
}

std::string EnvironmentManager::build_base_environment(const EnvironmentTemplate& tmpl) {
    // Create base directory for this template
    std::string base_path = cache_base_dir_ + "/base_" + tmpl.name;
    fs::create_directories(base_path);

    std::cout << "[EnvManager] Building environment at: " << base_path << std::endl;

    // Install packages using pip
    if (!tmpl.packages.empty()) {
        std::cout << "[EnvManager] Installing " << tmpl.packages.size()
                  << " packages..." << std::endl;

        if (!install_packages(base_path, tmpl.packages)) {
            throw std::runtime_error("Failed to install packages for: " + tmpl.name);
        }
    }

    // Run setup script if provided
    if (!tmpl.setup_script.empty()) {
        std::cout << "[EnvManager] Running setup script: " << tmpl.setup_script << std::endl;
        if (!run_setup_script(base_path, tmpl.setup_script)) {
            throw std::runtime_error("Setup script failed for: " + tmpl.name);
        }
    }

    std::cout << "[EnvManager] Environment built successfully: " << tmpl.name << std::endl;
    return base_path;
}

std::string EnvironmentManager::clone_environment(
    const std::string& base_path,
    const std::string& job_id
) {
    // Create job-specific directory
    std::string job_env_path = cache_base_dir_ + "/job_" + job_id;
    fs::create_directories(job_env_path);

    // For now, use simple recursive copy
    // TODO: Optimize with overlayfs or btrfs snapshots for true CoW
    std::string cmd = "cp -r " + base_path + "/* " + job_env_path + "/ 2>/dev/null || true";
    int ret = system(cmd.c_str());

    if (ret != 0) {
        std::cout << "[EnvManager] Warning: Copy failed, using empty directory" << std::endl;
    }

    return job_env_path;
}

bool EnvironmentManager::install_packages(
    const std::string& env_path,
    const std::vector<std::string>& packages
) {
    // Create a requirements.txt file
    std::string req_file = env_path + "/requirements.txt";
    std::ofstream req(req_file);
    for (const auto& pkg : packages) {
        req << pkg << "\n";
    }
    req.close();

    // Install packages using pip (in a way that works without venv for now)
    // Note: In production, this should use virtual environments
    std::string cmd = "pip3 install --target " + env_path + "/site-packages -r " +
                      req_file + " --quiet 2>&1 | tail -5";

    std::cout << "[EnvManager] Running: " << cmd << std::endl;
    int ret = system(cmd.c_str());

    // Check if installation succeeded (exit code 0)
    return ret == 0;
}

bool EnvironmentManager::run_setup_script(
    const std::string& env_path,
    const std::string& script_path
) {
    if (!fs::exists(script_path)) {
        std::cout << "[EnvManager] Setup script not found: " << script_path << std::endl;
        return false;
    }

    // Copy script to environment
    std::string local_script = env_path + "/setup.sh";
    fs::copy_file(script_path, local_script, fs::copy_options::overwrite_existing);

    // Make executable
    chmod(local_script.c_str(), 0755);

    // Run script in environment context
    std::string cmd = "cd " + env_path + " && bash setup.sh";
    int ret = system(cmd.c_str());

    return ret == 0;
}

void EnvironmentManager::cleanup_old_environments() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto it = cached_envs_.begin();

    while (it != cached_envs_.end()) {
        const auto& cached = it->second;
        const auto& tmpl = templates_[cached.template_name];

        auto age_hours = std::chrono::duration_cast<std::chrono::hours>(
            now - cached.created_at).count();

        if (age_hours >= tmpl.max_age_hours) {
            std::cout << "[EnvManager] Evicting old environment: "
                      << it->first << " (age: " << age_hours << "h)" << std::endl;

            // Delete cached environment
            fs::remove_all(cached.base_path);
            it = cached_envs_.erase(it);
        } else {
            ++it;
        }
    }

    // Also cleanup job-specific environments older than 1 hour
    if (fs::exists(cache_base_dir_)) {
        for (const auto& entry : fs::directory_iterator(cache_base_dir_)) {
            if (entry.path().filename().string().find("job_") == 0) {
                auto file_time = fs::last_write_time(entry);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    file_time - fs::file_time_type::clock::now() +
                    std::chrono::system_clock::now());

                auto age = std::chrono::system_clock::now() - sctp;
                if (std::chrono::duration_cast<std::chrono::hours>(age).count() >= 1) {
                    std::cout << "[EnvManager] Cleaning up old job environment: "
                              << entry.path().filename() << std::endl;
                    fs::remove_all(entry);
                }
            }
        }
    }
}

void EnvironmentManager::rebuild_template(const std::string& template_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove from cache
    auto it = cached_envs_.find(template_name);
    if (it != cached_envs_.end()) {
        fs::remove_all(it->second.base_path);
        cached_envs_.erase(it);
        std::cout << "[EnvManager] Removed cached environment: " << template_name << std::endl;
    }

    // Next prepare_environment() call will rebuild it
}

size_t EnvironmentManager::get_directory_size(const std::string& path) const {
    size_t size = 0;
    if (fs::exists(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (fs::is_regular_file(entry)) {
                size += fs::file_size(entry);
            }
        }
    }
    return size;
}

EnvironmentManager::Stats EnvironmentManager::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    stats.total_templates = templates_.size();
    stats.cached_environments = cached_envs_.size();
    stats.total_uses = 0;
    stats.disk_usage_mb = 0;

    for (const auto& [name, cached] : cached_envs_) {
        stats.total_uses += cached.use_count;
        stats.disk_usage_mb += get_directory_size(cached.base_path) / (1024 * 1024);
    }

    return stats;
}

// Built-in templates implementation

namespace BuiltInTemplates {

EnvironmentTemplate ml_basic() {
    EnvironmentTemplate tmpl;
    tmpl.name = "ml-basic";
    tmpl.base_image = "python:3.11";
    tmpl.packages = {
        "numpy",
        "pandas",
        "scikit-learn",
        "matplotlib"
    };
    tmpl.max_age_hours = 24;
    tmpl.gpu_enabled = false;
    return tmpl;
}

EnvironmentTemplate vision() {
    EnvironmentTemplate tmpl;
    tmpl.name = "vision";
    tmpl.base_image = "python:3.11";
    tmpl.packages = {
        "torch",
        "torchvision",
        "opencv-python",
        "Pillow"
    };
    tmpl.max_age_hours = 48;  // Keep longer due to large size
    tmpl.gpu_enabled = true;
    return tmpl;
}

EnvironmentTemplate nlp() {
    EnvironmentTemplate tmpl;
    tmpl.name = "nlp";
    tmpl.base_image = "python:3.11";
    tmpl.packages = {
        "torch",
        "transformers",
        "tokenizers",
        "sentencepiece"
    };
    tmpl.max_age_hours = 48;
    tmpl.gpu_enabled = true;
    return tmpl;
}

EnvironmentTemplate data_science() {
    EnvironmentTemplate tmpl;
    tmpl.name = "data-science";
    tmpl.base_image = "python:3.11";
    tmpl.packages = {
        "numpy",
        "pandas",
        "matplotlib",
        "seaborn",
        "jupyter",
        "ipython"
    };
    tmpl.max_age_hours = 24;
    tmpl.gpu_enabled = false;
    return tmpl;
}

EnvironmentTemplate scientific() {
    EnvironmentTemplate tmpl;
    tmpl.name = "scientific";
    tmpl.base_image = "python:3.11";
    tmpl.packages = {
        "numpy",
        "scipy",
        "sympy",
        "matplotlib"
    };
    tmpl.max_age_hours = 24;
    tmpl.gpu_enabled = false;
    return tmpl;
}

} // namespace BuiltInTemplates

} // namespace sandrun
