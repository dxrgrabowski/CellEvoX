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
        output_dir += "/";
    }
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
            file << "Generation,MeanFitness,FitnessVariance,MeanMutations,MutationsVariance,TotalLivingCells\n";
            for (size_t generation = 0; generation < run->generational_stat_report.size(); ++generation) {
                const auto& stat = run->generational_stat_report[generation];
                file << stat.tau << ","
                     << stat.mean_fitness << ","
                     << stat.fitness_variance << ","
                     << stat.mean_mutations << ","
                     << stat.mutations_variance << ","
                     << stat.total_living_cells << "\n";
            }
            file.close();
            std::cout << "Generational stats exported to: " << statFilename << std::endl;
        }
    }


    // Export Generational Population
    {
        std::string populFilename = output_dir + "generational_population.csv";
        std::ofstream file(populFilename);
        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << populFilename << std::endl;
        } else {
            file << "Generation,CellID,ParentID,Fitness,DeathTime,Mutations\n";
            for (const auto& [generation, cell_map] : run->generational_popul_report) {
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

                    file << generation << "," 
                        << cell_id << "," 
                        << cell_data.parent_id << "," 
                        << cell_data.fitness << "," 
                        << cell_data.death_time << ","
                        << "\"" << mutations_str << "\"\n"; 
                }
            }
            file.close();
            std::cout << "Generational population exported to: " << populFilename << std::endl;
        }
    }
    {
        std::string phylogenyFilename = output_dir + "phylogenic_tree.csv";
        std::ofstream file(phylogenyFilename);

        if (!file.is_open()) {
            std::cerr << "Cannot open file: " << phylogenyFilename << std::endl;
            return;
        }

        file << "NodeID,ParentID,ChildSum,DeathTime\n";

        for (const auto& [node_id, node_data] : run->phylogenic_tree) {
            file << node_id << "," 
                << node_data.parent_id << "," 
                << node_data.child_sum << "," 
                << node_data.death_time << "\n";
        }

        file.close();
        std::cout << "Phylogenic tree exported to: " << phylogenyFilename << std::endl;
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
    
    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);              
        mean_fitness.push_back(snapshot.mean_fitness);    
        fitness_variance.push_back(snapshot.fitness_variance); 
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
    plt::xlabel("Generation (tau)");
    plt::ylabel("Fitness Variance");
    plt::title("Fitness Variance Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "fitness_variance_over_generations.png");
}

void RunDataEngine::plotMutationsStatistics() {
    std::vector<double> generations;    
    std::vector<double> mean_mutations;  
    std::vector<double> mutations_variance; 
    
    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);              
        mean_mutations.push_back(snapshot.mean_mutations);    
        mutations_variance.push_back(snapshot.mutations_variance); 
    }
    
    plt::figure_size(800, 600);
    plt::plot(generations, mean_mutations, {{"label", "Mean Mutations (χs(t))"}});
    plt::xlabel("Generation");
    plt::ylabel("Mean Mutations");
    plt::title("Mean Mutations Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mean_mutations_over_generations.png");

    // Wykres wariancji mutations (σs^2(t))
    plt::figure_size(800, 600);
    plt::plot(generations, mutations_variance, {{"label", "Mutations Variance (σs²(t))"}});
    plt::xlabel("Generation (tau)");
    plt::ylabel("Mutations Variance");
    plt::title("Mutations Variance Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save(output_dir + "mutations_variance_over_generations.png");
}

void RunDataEngine::plotMutationWave() {
    for (const auto& [generation, cells] : run->generational_popul_report) {
        std::map<size_t, size_t> mutation_counts; // <liczba mutacji, liczba komórek>
        
        // Zliczanie komórek dla każdej liczby mutacji
        for (const auto& cell : cells) {
            size_t num_mutations = cell.second.mutations.size();
            mutation_counts[num_mutations]++;
        }

        // Przygotowanie danych do wykresu
        std::vector<size_t> mutation_bins; // Oś X: liczba mutacji
        std::vector<size_t> cell_counts;  // Oś Y: liczba komórek

        for (const auto& [mutations, count] : mutation_counts) {
            mutation_bins.push_back(mutations);
            cell_counts.push_back(count);
        }

        // Tworzenie wykresu słupkowego
        plt::figure_size(1000, 600); // Rozmiar wykresu
        plt::bar(mutation_bins, cell_counts, "green"); // Słupki w kolorze zielonym
        plt::xlabel("Number of Mutations");
        plt::ylabel("Number of Cells");
        plt::title("Mutation Wave: Distribution of Mutation Counts (Generation " + std::to_string(generation) + ")");
        plt::grid(true); // Dodaj siatkę
        plt::save(output_dir + "mutation_wave_histogram_generation_" + std::to_string(generation) + ".png"); // Zapisz wykres do pliku
    }
}

void RunDataEngine::exportPhylogenicTreeToGEXF(const std::string& filename)
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
    for (const auto& [node_id, node_data] : run->phylogenic_tree) {
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
    for (const auto& [node_id, node_data] : run->phylogenic_tree) {
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