#include <fstream>
#include <functional>
#include <iostream>
#include <math.h>
#include <memory>
#include <metis.h>
#include <string>
#include <toml11/toml.hpp>
#include <unordered_map>

#include "manager/manager.h"
#include "manager/cbase_manager.h"
#include "manager/early_min_cut_manager.h"
#include "manager/graph_cut_manager.h"
#include "manager/min_cut_manager.h"
#include "manager/tree_cut_manager.h"
#include "log/execution_log.h"
#include "partition/min_cut.h"
#include "request/request_generation.h"
#include "write/write.h"

typedef toml::basic_value<toml::discard_comments, std::unordered_map> toml_config;

using namespace std;

enum ManagerType {MIN_CUT, CBASE, EARLY_MIN_CUT};
const std::unordered_map<std::string, ManagerType> string_to_manager({
    {"MIN_CUT", ManagerType::MIN_CUT},
    {"CBASE", ManagerType::CBASE},
    {"EARLY_MIN_CUT", EARLY_MIN_CUT}
});

enum CutDataStructure {GRAPH, SPANNING_TREE};
const std::unordered_map<std::string, CutDataStructure> string_to_structure({
    {"GRAPH", CutDataStructure::GRAPH},
    {"SPANNING_TREE", CutDataStructure::SPANNING_TREE},
});

std::vector<workload::Request> generate_single_data_requests(
    const toml_config& config, workload::Manager& manager
) {
    const auto single_data_distributions = toml::find<std::vector<std::string>>(
        config, "workload", "requests", "single_data", "distribution_pattern"
    );
    const auto n_requests = toml::find<std::vector<int>>(
        config, "workload", "requests", "single_data", "n_requests"
    );

    auto requests = std::vector<workload::Request>();
    auto binomial_counter = 0;
    for (auto i = 0; i < n_requests.size(); i++) {
        auto current_distribution_ = single_data_distributions[i];
        auto current_distribution = rfunc::string_to_distribution.at(
            current_distribution_
        );

        auto new_requests = std::vector<workload::Request>();
        if (current_distribution == rfunc::FIXED) {
            const auto requests_per_data = floor(n_requests[i]/manager.n_variables());

            new_requests = workload::generate_fixed_data_requests(
                manager.n_variables(), requests_per_data
            );
        } else if (current_distribution == rfunc::UNIFORM) {
            auto data_rand = rfunc::uniform_distribution_rand(
                0, manager.n_variables()-1
            );

            new_requests = workload::generate_single_data_requests(
                n_requests[i], data_rand
            );
        } else if (current_distribution == rfunc::BINOMIAL) {
            const auto success_probability = toml::find<std::vector<double>>(
                config, "workload", "requests", "single_data", "success_probability"
            );
            auto data_rand = rfunc::binomial_distribution(
                manager.n_variables()-1, success_probability[binomial_counter]
            );

            new_requests = workload::generate_single_data_requests(
                n_requests[i], data_rand
            );
            binomial_counter++;
        }

        requests.insert(requests.end(), new_requests.begin(), new_requests.end());
    }

    return requests;
}

std::vector<workload::Request> generate_multi_data_requests(
    const toml_config& config, workload::Manager& manager
) {
    const auto n_requests = toml::find<std::vector<int>>(
        config, "workload", "requests", "multi_data", "n_requests"
    );

    const auto min_involved_data = toml::find<std::vector<int>>(
        config, "workload", "requests", "multi_data",
        "min_involved_data"
    );
    const auto max_involved_data = toml::find<std::vector<int>>(
        config, "workload", "requests", "multi_data",
        "max_involved_data"
    );

    const auto size_distribution_ = toml::find<std::vector<std::string>>(
        config, "workload", "requests", "multi_data", "size_distribution_pattern"
    );
    const auto data_distribution_ = toml::find<std::vector<std::string>>(
        config, "workload", "requests", "multi_data", "data_distribution_pattern"
    );

    auto size_binomial_counter = 0;
    auto data_binomial_counter = 0;
    auto requests = std::vector<workload::Request>();
    for (auto i = 0; i < n_requests.size(); i++) {
        auto new_requests = std::vector<workload::Request>();

        auto size_distribution = rfunc::string_to_distribution.at(
            size_distribution_[i]
        );
        rfunc::RandFunction size_rand;
        if (size_distribution == rfunc::UNIFORM) {
            size_rand = rfunc::uniform_distribution_rand(
                min_involved_data[i], max_involved_data[i]
            );
        } else if (size_distribution == rfunc::FIXED) {
            size_rand = rfunc::fixed_distribution(max_involved_data[i]);
        } else if (size_distribution == rfunc::BINOMIAL) {
            const auto success_probability = toml::find<std::vector<double>>(
                config, "workload", "requests", "multi_data",
                "size_success_probability"
            );
            size_rand = rfunc::ranged_binomial_distribution(
                min_involved_data[i], max_involved_data[i],
                success_probability[size_binomial_counter]
            );
            size_binomial_counter++;
        }

        auto data_distribution = rfunc::string_to_distribution.at(
            data_distribution_[i]
        );
        rfunc::RandFunction data_rand;
        if (data_distribution == rfunc::UNIFORM) {
            data_rand = rfunc::uniform_distribution_rand(0, manager.n_variables()-1);
        } else if (data_distribution == rfunc::BINOMIAL) {
            const auto success_probability = toml::find<std::vector<double>>(
                config, "workload", "requests", "multi_data",
                "data_success_probability"
            );
            data_rand = rfunc::binomial_distribution(
                manager.n_variables()-1, success_probability[data_binomial_counter]
            );
            data_binomial_counter++;
        }

        new_requests = workload::generate_multi_data_requests(
            n_requests[i],
            manager.n_variables(),
            data_rand,
            size_rand
        );
        requests.insert(requests.end(), new_requests.begin(), new_requests.end());
    }

    return requests;
}

void generate_random_requests(
    const toml_config& config, workload::Manager& manager
) {
    auto single_data_requests = generate_single_data_requests(config, manager);
    auto multi_data_requests = generate_multi_data_requests(config, manager);

    const auto single_data_pick_probability = toml::find<int>(
        config, "workload", "requests", "single_data_pick_probability"
    );
    auto requests = workload::merge_requests(
        single_data_requests,
        multi_data_requests,
        single_data_pick_probability
    );

    manager.set_requests(requests);
}

void import_requests(const toml_config& config, workload::Manager& manager) {
    auto import_path = toml::find<std::string>(
        config, "workload", "requests", "import_path"
    );
    auto requests = std::move(workload::import_requests(import_path, manager.n_variables()));
    //std::cout << "Imported" << requests.size() << " requests" << std::endl;
    manager.set_requests(requests);
}

std::unique_ptr<workload::CBaseManager> create_cbase_cut_manager(
    const toml_config& config, int n_variables
) {
    const auto n_threads = toml::find<int>(
        config, "execution", "n_threads"
    );

    return std::make_unique<workload::CBaseManager>(
        workload::CBaseManager(n_variables, n_threads)
    );
}

std::unique_ptr<workload::GraphCutManager> create_graph_cut_manager(
    const toml_config& config, int n_variables
) {
    const auto n_partitions = toml::find<int>(
        config, "workload", "initial_partitions", "n_partitions"
    );

    const auto repartition_interval = toml::find<int>(
        config, "execution", "repartition_interval"
    );

    const auto should_import_partitions = toml::find<bool>(
        config, "workload", "initial_partitions", "import"
    );

    const auto cut_method_name = toml::find<std::string>(
        config, "execution", "cut_method"
    );
    const auto cut_method = model::string_to_cut_method.at(cut_method_name);

    if (should_import_partitions) {
        const auto path = toml::find<std::string>(
            config, "workload", "initial_partitions", "path"
        );
        const auto initial_partitions_file = toml::parse(path);
        auto data_partitions = toml::find<std::vector<int>>(
            initial_partitions_file, "data_partitions"
        );
        return std::make_unique<workload::GraphCutManager>(
            workload::GraphCutManager(
                n_variables, n_partitions, repartition_interval,
                data_partitions, cut_method
            )
        );
    }

    return std::make_unique<workload::GraphCutManager>(
        workload::GraphCutManager(
            n_variables, n_partitions, repartition_interval, cut_method
        )
    );
}

std::unique_ptr<workload::TreeCutManager> create_tree_cut_manager(
    const toml_config& config, int n_variables
) {
    const auto n_partitions = toml::find<int>(
        config, "workload", "initial_partitions", "n_partitions"
    );

    auto repartition_interval = 0;
    const auto should_repartition_during_execution = toml::find<bool>(
        config, "execution", "repartition_during_execution"
    );
    if (should_repartition_during_execution) {
        repartition_interval = toml::find<int>(
            config, "execution", "repartition_interval"
        );
    }

    const auto should_import_partitions = toml::find<bool>(
        config, "workload", "initial_partitions", "import"
    );

    if (should_import_partitions) {
        const auto path = toml::find<std::string>(
            config, "workload", "initial_partitions", "path"
        );
        const auto initial_partitions_file = toml::parse(path);
        auto data_partitions = toml::find<std::vector<int>>(
            initial_partitions_file, "data_partitions"
        );
        return std::make_unique<workload::TreeCutManager>(
            workload::TreeCutManager(
                n_variables, n_partitions, repartition_interval,
                data_partitions
            )
        );
    }

    return std::make_unique<workload::TreeCutManager>(
        workload::TreeCutManager(
            n_variables, n_partitions, repartition_interval
        )
    );
}

std::unique_ptr<workload::MinCutManager> create_min_cut_manager(
    const toml_config& config, int n_variables
) {

    const auto data_structure_name = toml::find<std::string>(
        config, "execution", "data_structure"
    );
    const auto data_structure = string_to_structure.at(data_structure_name);

    if (data_structure == CutDataStructure::GRAPH) {
        return create_graph_cut_manager(config, n_variables);
    } else {
        return create_tree_cut_manager(config, n_variables);
    }
}

std::unique_ptr<workload::EarlyMinCutManager> create_early_min_cut_manager(
    const toml_config& config, int n_variables
) {
    const auto repartition_interval = toml::find<int>(
        config, "execution", "repartition_interval"
    );
    const auto n_partitions = toml::find<int>(
        config, "workload", "initial_partitions", "n_partitions"
    );

    return std::make_unique<workload::EarlyMinCutManager>(
        workload::EarlyMinCutManager(n_variables, n_partitions, repartition_interval)
    );
}

void export_requests(const toml_config& config, workload::Manager& manager) {
    const auto output_path = toml::find<std::string>(
        config, "output", "requests", "path"
    );
    std::ofstream ofs(output_path, std::ofstream::out);
    manager.export_requests(ofs);
    ofs.close();
}

void export_execution_info(
    const toml_config& config,
    workload::ExecutionLog& execution_log
) {
    const auto output_path = toml::find<std::string>(
        config, "output", "info_path"
    );
    std::ofstream output_stream(output_path, std::ofstream::out);
    output::write_log_info(execution_log, output_stream);
    output_stream.close();
}

int main(int argc, char* argv[]) {
    const auto config = toml::parse(argv[1]);

    const auto manager_type_ = toml::find<std::string>(
        config, "execution", "manager"
    );
    const auto manager_type = string_to_manager.at(manager_type_);

    const auto n_variables = toml::find<int>(
        config, "workload", "n_variables"
    );
    auto manager = [&]() -> std::unique_ptr<workload::Manager>{
        if (manager_type == ManagerType::MIN_CUT) {
            return create_min_cut_manager(config, n_variables);
        } else if (manager_type == EARLY_MIN_CUT) {
            return create_early_min_cut_manager(config, n_variables);
        }

        return create_cbase_cut_manager(config, n_variables);
    }();

    const auto should_import_requests = toml::find<bool>(
        config, "workload", "requests", "import_requests"
    );
    if (should_import_requests) {
        import_requests(config, *manager);
    } else {
        generate_random_requests(config, *manager);
    }

    const auto should_export_requests = toml::find<bool>(
        config, "output", "requests", "export"
    );
    if (should_export_requests) {
        export_requests(config, *manager);
    }

    const auto should_execute_requests = toml::find<bool>(
        config, "execution", "execute_requests"
    );
    if (not should_execute_requests) {
        return 0;
    }

    auto execution_log = manager->execute_requests();
    export_execution_info(config, execution_log);

    return 0;
}
