#include "core/RunDataEngine.hpp"
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>
#include <map>
#include <external/matplotlibcpp.h>
#include <random>
#include <tbb/parallel_for.h>
#include <tbb/concurrent_hash_map.h>
#include <fstream>
#include <iostream>
#include <tbb/concurrent_unordered_set.h>
#include <filesystem>
namespace CellEvoX::core {

namespace plt = matplotlibcpp;

RunDataEngine::RunDataEngine(
    std::shared_ptr<SimulationConfig> config, 
    std::shared_ptr<ecs::Run> run, 
    double generation_step)
    : config(config), run(run), 
    generation_step(generation_step) {
        prepareOutputDir();
    }

void RunDataEngine::prepareOutputDir()
{
    output_dir = config->output_path;
    if (!std::filesystem::exists(output_dir) && output_dir != "") {
        std::filesystem::create_directories(output_dir);
    }
    output_dir += "/";
}
void RunDataEngine::exportToCSV() 
{
    // Export Generational Statistics
    {
        std::string statFilename = output_dir + "generational_statistics.csv";
        std::ofstream file(statFilename);
        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << statFilename << std::endl;
        } else {
            // Write header row with all fields
            file << "Generation,TotalLivingCells,MeanFitness,FitnessVariance,FitnessSkewness,FitnessKurtosis,"
                << "MeanMutations,MutationsVariance,MutationsSkewness,MutationsKurtosis\n";

            // Write data rows
            for (size_t generation = 0; generation < run->generational_stat_report.size(); ++generation) {
                const auto& stat = run->generational_stat_report[generation];
                file << stat.tau << ","
                    << stat.total_living_cells << ","
                    << stat.mean_fitness << ","
                    << stat.fitness_variance << ","
                    << stat.fitness_skewness << ","
                    << stat.fitness_kurtosis << ","
                    << stat.mean_mutations << ","
                    << stat.mutations_variance << ","
                    << stat.mutations_skewness << ","
                    << stat.mutations_kurtosis << "\n";
            }

            file.close();
            std::cout << "Generational stats exported to: " << statFilename << std::endl;
        }
    }

    // Export Generational Population (Separate Files for Each Generation)
    {
        for (const auto& [generation, cell_map] : run->generational_popul_report) {
            std::string populFilename = output_dir + "population_generation_" + std::to_string(generation) + ".csv";
            std::ofstream file(populFilename);
            if (!file.is_open()) {
                std::cerr << "Cannot open file: " << populFilename << std::endl;
                continue;
            }

            // Write the header
            file << "CellID,ParentID,Fitness,Mutations\n";

            // Write data for each cell in this generation
            for (const auto& [cell_id, cell_data] : cell_map) {
                // Retrieve mutations as a string
                std::string mutations_str;
                for (const auto& [mutation_id, mutation_type] : cell_data.mutations) {
                    mutations_str += "(" + std::to_string(mutation_id) + "," + std::to_string(mutation_type) + ") ";
                }

                // Trim the trailing space
                if (!mutations_str.empty()) {
                    mutations_str.pop_back();
                }

                file << cell_id << ","
                    << cell_data.parent_id << ","
                    << cell_data.fitness << ","
                    << "\"" << mutations_str << "\"\n";
            }

            file.close();
            std::cout << "Population data exported to: " << populFilename << std::endl;
        }
    }

    {
        std::string phylogenyFilename = output_dir + "phylogenetic_tree.csv";
        std::ofstream file(phylogenyFilename);

        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << phylogenyFilename << std::endl;
            return;
        }

        file << "NodeID,ParentID,ChildSum,DeathTime\n";

        for (const auto& [node_id, node_data] : run->phylogenetic_tree) {
            file << node_id << "," 
                << node_data.parent_id << "," 
                << node_data.child_sum << "," 
                << node_data.death_time << "\n";
        }

        file.close();
        std::cout << "Phylogenetic tree exported to: " << phylogenyFilename << std::endl;
    }

}
void RunDataEngine::plotLivingCellsOverGenerations() {
    std::vector<double> generations;
    std::vector<size_t> living_cells; 
    
    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);               
        living_cells.push_back(snapshot.total_living_cells); 
    }
    
    plt::figure_size(800, 600); 
    plt::plot(generations, living_cells, "g-"); 
    plt::xlabel("Generation");
    plt::ylabel("Total Living Cells");
    plt::title("Number of Living Cells Over Generations");
    plt::grid(true); // Dodaj siatkę
    plt::save(output_dir + "living_cells_over_generations.png");
}
void RunDataEngine::plotFitnessStatistics() {
    std::vector<double> generations;    
    std::vector<double> mean_fitness;   
    std::vector<double> fitness_variance; 
    std::vector<double> fitness_skewness;
    std::vector<double> fitness_kurtosis;

    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);              
        mean_fitness.push_back(snapshot.mean_fitness);    
        fitness_variance.push_back(snapshot.fitness_variance); 
        fitness_skewness.push_back(snapshot.fitness_skewness); 
        fitness_kurtosis.push_back(snapshot.fitness_kurtosis); 
    }

    plt::figure_size(800, 600);
    plt::plot(generations, mean_fitness, {{"label", "Mean Fitness (χs(t))"}}); 
    plt::xlabel("Generation");
    plt::ylabel("Mean Fitness");
    plt::title("Mean Fitness Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mean_fitness_over_generations.png");

    plt::figure_size(800, 600);
    plt::plot(generations, fitness_variance, {{"label", "Fitness Variance (σs²(t))"}});
    plt::xlabel("Generation");
    plt::ylabel("Fitness Variance");
    plt::title("Fitness Variance Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "fitness_variance_over_generations.png");

    plt::figure_size(800, 600);
    plt::plot(generations, fitness_skewness, {{"label", "Fitness Skewness"}});
    plt::xlabel("Generation");
    plt::ylabel("Skewness");
    plt::title("Fitness Skewness Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "fitness_skewness_over_generations.png");

    plt::figure_size(800, 600);
    plt::plot(generations, fitness_kurtosis, {{"label", "Fitness Kurtosis"}});
    plt::xlabel("Generation");
    plt::ylabel("Kurtosis");
    plt::title("Fitness Kurtosis Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "fitness_kurtosis_over_generations.png");
}

void RunDataEngine::plotMutationsStatistics() {
    std::vector<double> generations;    
    std::vector<double> mean_mutations;  
    std::vector<double> mutations_variance; 
    std::vector<double> mutations_skewness;
    std::vector<double> mutations_kurtosis;

    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);              
        mean_mutations.push_back(snapshot.mean_mutations);    
        mutations_variance.push_back(snapshot.mutations_variance); 
        mutations_skewness.push_back(snapshot.mutations_skewness);
        mutations_kurtosis.push_back(snapshot.mutations_kurtosis);
    }

    plt::figure_size(800, 600);
    plt::plot(generations, mean_mutations, {{"label", "Mean Mutations (χs(t))"}});
    plt::xlabel("Generation");
    plt::ylabel("Mean Mutations");
    plt::title("Mean Mutations Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mean_mutations_over_generations.png");

    plt::figure_size(800, 600);
    plt::plot(generations, mutations_variance, {{"label", "Mutations Variance (σs²(t))"}});
    plt::xlabel("Generation");
    plt::ylabel("Mutations Variance");
    plt::title("Mutations Variance Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mutations_variance_over_generations.png");

    // Plot Mutations Skewness
    plt::figure_size(800, 600);
    plt::plot(generations, mutations_skewness, {{"label", "Mutations Skewness"}});
    plt::xlabel("Generation");
    plt::ylabel("Skewness");
    plt::title("Mutations Skewness Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mutations_skewness_over_generations.png");

    // Plot Mutations Kurtosis
    plt::figure_size(800, 600);
    plt::plot(generations, mutations_kurtosis, {{"label", "Mutations Kurtosis"}});
    plt::xlabel("Generation");
    plt::ylabel("Kurtosis");
    plt::title("Mutations Kurtosis Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mutations_kurtosis_over_generations.png");
}


void RunDataEngine::plotMutationWave() {
    for (const auto& [generation, cells] : run->generational_popul_report) {
        std::map<size_t, size_t> mutation_counts; // <number of mutations, number of cells>
        
        for (const auto& cell : cells) {
            size_t num_mutations = cell.second.mutations.size();
            mutation_counts[num_mutations]++;
        }

        std::vector<size_t> mutation_bins;
        std::vector<size_t> cell_counts;  

        for (const auto& [mutations, count] : mutation_counts) {
            mutation_bins.push_back(mutations);
            cell_counts.push_back(count);
        }

        plt::figure_size(1000, 600); 
        plt::bar(mutation_bins, cell_counts, "green"); 
        plt::xlabel("Number of Mutations");
        plt::ylabel("Number of Cells");
        plt::title("Mutation Wave: Distribution of Mutation Counts (Generation " + std::to_string(generation) + ")");
        plt::grid(true);
        plt::save(output_dir + "mutation_wave_histogram_generation_" + std::to_string(generation) + ".png");
    }
}

void RunDataEngine::plotMutationFrequency() {
    for (const auto& [generation, cells] : run->generational_popul_report) {
        std::map<uint32_t, uint32_t> mutation_counts;
        uint32_t total_cells = 0;

        for (const auto& item : cells) {
            const Cell& cell = item.second;
            ++total_cells;

            for (const auto& mutation : cell.mutations) {
                uint32_t mutation_id = mutation.first;
                mutation_counts[mutation_id]++;
            }
        }

        std::vector<double> vafs;
        for (const auto& [mutation_id, count] : mutation_counts) {
            double vaf = static_cast<double>(count) / total_cells;
            vafs.push_back(vaf);
        }

        int num_bins = std::max(1, static_cast<int>(std::ceil(1 + 3.322 * std::log10(vafs.size()))));

        plt::figure();
        plt::hist(vafs, num_bins); 
        plt::title("VAF Histogram - Generation " + std::to_string(generation));
        plt::xlabel("Variant Allele Frequency (VAF)");
        plt::ylabel("Frequency");
        plt::save(output_dir + "vaf_histogram_generation_" + std::to_string(generation) + ".png");
    }
}

void RunDataEngine::exportPhylogeneticTreeToGEXF(const std::string& filename)
{
    std::string output_file = output_dir + filename;
    std::ofstream file(output_file);

    if (!file.is_open()) {
        std::cerr << "Nie można otworzyć pliku: " << output_file << std::endl;
        return;
    }

    file << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
    file << R"(<gexf xmlns="http://www.gexf.net/1.3" version="1.3">)" << "\n";
    file << R"(<graph mode="static" defaultedgetype="directed">)" << "\n";

    file << R"(<attributes class="node">)" << "\n";
    file << R"(<attribute id="0" title="status" type="string"/>)" << "\n";
    file << R"(<attribute id="2" title="parent_id" type="integer"/>)" << "\n";
    file << R"(<attribute id="3" title="child_sum" type="integer"/>)" << "\n";
    file << R"(</attributes>)" << "\n";
    
    file << R"(<nodes>)" << "\n";
    for (const auto& [node_id, node_data] : run->phylogenetic_tree) {
        CellMap::const_accessor cell_accessor;
        bool is_alive = run->cells.find(cell_accessor, node_id);
        std::string status = is_alive ? "ALIVE" : "DEAD";

        file << R"(<node id=")" << node_id << R"(" label="Node )" << node_id << R"(">)" << "\n";
        file << R"(<attvalues>)" << "\n";
        file << R"(<attvalue for="0" value=")" << status << R"("/>)" << "\n";
        file << R"(<attvalue for="2" value=")" << node_data.parent_id << R"("/>)" << "\n";
        file << R"(<attvalue for="3" value=")" << node_data.child_sum << R"("/>)" << "\n";
        file << R"(</attvalues>)" << "\n";
        file << R"(</node>)" << "\n";
    }
    file << R"(</nodes>)" << "\n";

    file << R"(<edges>)" << "\n";
    uint32_t edge_id = 0;
    for (const auto& [node_id, node_data] : run->phylogenetic_tree) {
        if (node_data.parent_id != 0) {
            file << R"(<edge id=")" << edge_id++ << R"(" source=")" 
                 << node_data.parent_id << R"(" target=")" << node_id << R"("/>)" << "\n";
        } else if (node_id != 0) {
            file << R"(<edge id=")" << edge_id++ << R"(" source=")" 
                 << 0 << R"(" target=")" << node_id << R"("/>)" << "\n";
        }
    }
    file << R"(</edges>)" << "\n";

    file << R"(</graph>)" << "\n";
    file << R"(</gexf>)" << "\n";

    file.close();
    std::cout << "Graf zapisany do pliku: " << output_file << std::endl;
}

void RunDataEngine::exportGenealogyToGexf(size_t num_cells_to_trace, const std::string& filename) {
    tbb::concurrent_vector<uint32_t> selected_cells;
    tbb::concurrent_unordered_set<uint32_t> visited_nodes;
    tbb::concurrent_vector<std::pair<uint32_t, uint32_t>> edges;
    tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>> node_attributes;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, run->cells.size() - 1);

    std::vector<uint32_t> cell_ids;
    for (auto it = run->cells.begin(); it != run->cells.end(); ++it) {
        cell_ids.push_back(it->first);
    }

    for (size_t i = 0; i < num_cells_to_trace; ++i) {
        uint32_t idx = dis(gen);
        selected_cells.push_back(cell_ids[idx]);
    }

    tbb::parallel_for(size_t(0), selected_cells.size(), [&](size_t i) {
        uint32_t current_id = selected_cells[i];
        while (current_id != 0) {
            uint32_t parent_id = 0;

             {
                CellMap::const_accessor cell_accessor;
                if (run->cells.find(cell_accessor, current_id)) {
                    parent_id = cell_accessor->second.parent_id;

                    tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>>::accessor attr_accessor;
                    if (node_attributes.insert(attr_accessor, current_id)) {
                        attr_accessor->second = {"alive", 0.0};
                    }
                } else {
                    Graveyard::const_accessor grave_accessor;
                    if (run->cells_graveyard.find(grave_accessor, current_id)) {
                        parent_id = grave_accessor->second.first;

                        tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>>::accessor attr_accessor;
                        if (node_attributes.insert(attr_accessor, current_id)) {
                            attr_accessor->second = {"dead", grave_accessor->second.second};
                        }
                    }
                }
            }

            if (parent_id != 0) {
                edges.emplace_back(parent_id, current_id);
                visited_nodes.insert(current_id);
                visited_nodes.insert(parent_id);
            }

            current_id = parent_id;
        }
    });

    std::ofstream gexf_file(filename);
    if (!gexf_file.is_open()) {
        return;
    }

    gexf_file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    gexf_file << "<gexf xmlns=\"http://www.gexf.net/1.3\" version=\"1.3\">\n";
    gexf_file << "  <graph mode=\"static\" defaultedgetype=\"directed\">\n";
    gexf_file << "    <attributes class=\"node\">\n";
    gexf_file << "      <attribute id=\"0\" title=\"status\" type=\"string\"/>\n";
    gexf_file << "      <attribute id=\"1\" title=\"death_time\" type=\"double\"/>\n";
    gexf_file << "    </attributes>\n";
    gexf_file << "    <nodes>\n";
   for (const auto& node : visited_nodes) {
        std::string status = "unknown";
        double death_time = -1.0;

        tbb::concurrent_hash_map<uint32_t, std::pair<std::string, double>>::const_accessor attr_accessor;
        if (node_attributes.find(attr_accessor, node)) {
            status = attr_accessor->second.first;
            death_time = attr_accessor->second.second;
        }

        gexf_file << "      <node id=\"" << node << "\" label=\"Cell " << node << "\">\n";
        gexf_file << "        <attvalues>\n";
        gexf_file << "          <attvalue for=\"0\" value=\"" << status << "\"/>\n";
        gexf_file << "          <attvalue for=\"1\" value=\"" << death_time << "\"/>\n";
        gexf_file << "        </attvalues>\n";
        gexf_file << "      </node>\n";
    }
    gexf_file << "    </nodes>\n";
    gexf_file << "    <edges>\n";
    size_t edge_id = 0;
    for (const auto& edge : edges) {
        gexf_file << "      <edge id=\"" << edge_id++ << "\" source=\"" << edge.first
                    << "\" target=\"" << edge.second << "\"/>\n";
    }
    gexf_file << "    </edges>\n";
    gexf_file << "  </graph>\n";
    gexf_file << "</gexf>\n";
}                  
} // namespace core