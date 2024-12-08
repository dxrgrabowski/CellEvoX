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
namespace CellEvoX::core {

namespace plt = matplotlibcpp;

RunDataEngine::RunDataEngine(
    std::shared_ptr<SimulationConfig> config, 
    std::shared_ptr<ecs::Run> run, 
    double generation_step)
    : config(config), run(run), 
    generation_step(generation_step) {}

void RunDataEngine::exportToCharts() 
{

}
void RunDataEngine::plotLivingCellsOverGenerations() {
    // Przygotowanie danych
    std::vector<double> generations; // Oś X - numer generacji (tau)
    std::vector<size_t> living_cells; // Oś Y - liczba żyjących komórek
    
    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);                // Generacja (tau)
        living_cells.push_back(snapshot.total_living_cells); // Liczba komórek
    }
    
    // Rysowanie wykresu
    plt::figure_size(800, 600); // Ustaw rozmiar wykresu
    plt::plot(generations, living_cells, "g-"); // "g-" oznacza zieloną linię
    plt::xlabel("Generation");
    plt::ylabel("Total Living Cells");
    plt::title("Number of Living Cells Over Generations");
    plt::grid(true); // Dodaj siatkę
    plt::save("living_cells_over_generations.png"); // Zapisz wykres do pliku
    plt::show(); // Wyświetl wykres
}

void RunDataEngine::plotFitnessStatistics() {
    // Przygotowanie danych
    std::vector<double> generations;    // Oś X - numer generacji (tau)
    std::vector<double> mean_fitness;   // Oś Y dla średniej wartości fitness
    std::vector<double> fitness_variance; // Oś Y dla wariancji fitness
    
    for (const auto& snapshot : run->generational_stat_report) {
        generations.push_back(snapshot.tau);              // Generacja (tau)
        mean_fitness.push_back(snapshot.mean_fitness);    // Średnia fitness
        fitness_variance.push_back(snapshot.fitness_variance); // Wariancja fitness
    }
    
    // Wykres średniej fitness (χs(t))
    plt::figure_size(800, 600);
    plt::plot(generations, mean_fitness, {{"label", "Mean Fitness (χs(t))"}}); // "r-" oznacza czerwoną linię
    plt::xlabel("Generation");
    plt::ylabel("Mean Fitness");
    plt::title("Mean Fitness Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save("mean_fitness_over_generations.png");
    plt::show();

    // Wykres wariancji fitness (σs^2(t))
    plt::figure_size(800, 600);
    plt::plot(generations, fitness_variance, {{"label", "Fitness Variance (σs²(t))"}}); // "b-" oznacza niebieską linię
    plt::xlabel("Generation (tau)");
    plt::ylabel("Fitness Variance");
    plt::title("Fitness Variance Over Generations");
    plt::legend();
    plt::grid(true);
    plt::save("fitness_variance_over_generations.png");
    plt::show();
}

void RunDataEngine::plotMutationWave() {
    std::map<size_t, size_t> mutation_counts; // <liczba mutacji, liczba komórek>
    
    // Zliczanie komórek dla każdej liczby mutacji
    for (const auto& cell : run->cells) {
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
    plt::title("Mutation Wave: Distribution of Mutation Counts");
    plt::grid(true); // Dodaj siatkę
    plt::save("mutation_wave_histogram.png"); // Zapisz wykres do pliku
    plt::show(); // Wyświetl wykres
}

void RunDataEngine::exportPhylogenicTreeToGEXF(const std::string& filename)
{
    std::ofstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Nie można otworzyć pliku: " << filename << std::endl;
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
    std::cout << "Graf zapisany do pliku: " << filename << std::endl;
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