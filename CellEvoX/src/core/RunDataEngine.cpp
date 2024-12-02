#include "core/RunDataEngine.hpp"
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>
#include <map>
#include <external/matplotlibcpp.h>

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
    
    for (const auto& snapshot : run->generational_report) {
        generations.push_back(snapshot.tau);                // Generacja (tau)
        living_cells.push_back(snapshot.total_living_cells); // Liczba komórek
    }
    
    // Rysowanie wykresu
    plt::figure_size(800, 600); // Ustaw rozmiar wykresu
    plt::plot(generations, living_cells, "g-"); // "g-" oznacza zieloną linię
    plt::xlabel("Generation (tau)");
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
    
    for (const auto& snapshot : run->generational_report) {
        generations.push_back(snapshot.tau);              // Generacja (tau)
        mean_fitness.push_back(snapshot.mean_fitness);    // Średnia fitness
        fitness_variance.push_back(snapshot.fitness_variance); // Wariancja fitness
    }
    
    // Wykres średniej fitness (χs(t))
    plt::figure_size(800, 600);
    plt::plot(generations, mean_fitness, {{"label", "Mean Fitness (χs(t))"}}); // "r-" oznacza czerwoną linię
    plt::xlabel("Generation (tau)");
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

} // namespace core