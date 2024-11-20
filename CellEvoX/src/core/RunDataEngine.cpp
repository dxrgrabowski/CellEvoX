#include "core/RunDataEngine.hpp"
#include <sstream>
#include <cmath>
#include <spdlog/spdlog.h>
#include <map>

namespace CellEvoX::core {

RunDataEngine::RunDataEngine(double generation_step)
    : generation_step(generation_step) {}

std::vector<std::vector<const Cell*>> RunDataEngine::classifyByGeneration(const tbb::concurrent_vector<Cell>& cells) {
    // Determine layers based on generation (birth time / tau)
    std::map<int, std::vector<const Cell*>> layers;

    for (const auto& cell : cells) {
        int generation = static_cast<int>(std::floor(cell.birth_time / generation_step));
        layers[generation].push_back(&cell);
    }

    // Convert map to vector (sorted by generation)
    std::vector<std::vector<const Cell*>> classified;
    for (const auto& [gen, cells_in_layer] : layers) {
        classified.push_back(cells_in_layer);
    }

    return classified;
}

void RunDataEngine::generateGraphvizDot(const tbb::concurrent_vector<Cell>& cells, const std::string& output_path) {
    auto generations = classifyByGeneration(cells);

    std::ofstream dot_file(output_path);
    if (!dot_file.is_open()) {
        spdlog::error("Failed to open file for Graphviz output: {}", output_path);
        return;
    }

    dot_file << "digraph PopulationGraph {\n";
    dot_file << "  rankdir=LR;\n"; // Use left-to-right direction for better circular layout

    // Add nodes for each cell
    for (size_t gen = 0; gen < generations.size(); ++gen) {
        for (const auto* cell : generations[gen]) {
            dot_file << "  Cell" << cell->id
                     << " [label=\"ID: " << cell->id << "\\nParent: " << cell->parent_id
                     << "\\nTau: " << cell->birth_time << "\"];\n";
        }
    }

    // Add edges to represent parent-child relationships
    for (const auto& cell : cells) {
        if (cell.parent_id >= 0) { // Assuming parent_id is -1 for root cells
            dot_file << "  Cell" << cell.parent_id << " -> Cell" << cell.id << ";\n";
        }
    }

    // Add subgraph for circular layout
    for (size_t gen = 0; gen < generations.size(); ++gen) {
        dot_file << "  subgraph cluster_gen" << gen << " {\n";
        dot_file << "    label=\"Generation " << gen << "\";\n";
        dot_file << "    style=dotted;\n";
        for (const auto* cell : generations[gen]) {
            dot_file << "    Cell" << cell->id << ";\n";
        }
        dot_file << "  }\n";
    }

    dot_file << "}\n";
    dot_file.close();

    spdlog::info("Graphviz DOT file written to {}", output_path);
}

void RunDataEngine::exportToGEXF(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file, const double& sim_end) {
    const double layer_step = 0.005;  // Step for each layer in tau
    const double radius_increment = 100.0;  // Radius increment per layer (arbitrary, tune as needed)
    const double angle_increment = M_PI / 180.0;  // Angle increment in radians (1 degree)

    // Calculate positions for each node
    std::unordered_map<size_t, std::pair<double, double>> node_positions;  // Map: cell.id -> (x, y)
    std::unordered_map<double, size_t> layer_counts;  // Map: tau layer -> count of nodes in the layer

    for (const auto& cell : cells) {
        double layer = std::floor(cell.birth_time / layer_step) * layer_step;  // Determine the layer
        size_t count_in_layer = ++layer_counts[layer];  // Get the position in the layer

        double radius = layer / layer_step * radius_increment;  // Radius of the layer
        double angle = count_in_layer * angle_increment;        // Angle for this node

        double x = radius * std::cos(angle);
        double y = radius * std::sin(angle);
        node_positions[cell.id] = {x, y};
    }

    // Write GEXF file
    std::ofstream gexf(output_file);
    if (!gexf.is_open()) {
        spdlog::error("Failed to open file for GEXF output: {}", output_file);
        return;
    }

    // Header with viz namespace
    gexf << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    gexf << "<gexf xmlns=\"http://www.gexf.net/1.3\" xmlns:viz=\"http://www.gexf.net/1.3/viz\" version=\"1.3\">\n";
    gexf << "  <graph mode=\"dynamic\" defaultedgetype=\"directed\">\n";

    // Nodes with positions
    gexf << "    <nodes>\n";
    for (const auto& cell : cells) {
        const auto& [x, y] = node_positions[cell.id];
        gexf << "      <node id=\"" << cell.id
             << "\" label=\"Cell " << cell.id
             << "\" start=\"" << cell.birth_time
             << "\" end=\"" << sim_end << "\">\n";
        gexf << "        <viz:position x=\"" << x << "\" y=\"" << y << "\" z=\"0.0\"/>\n";
        gexf << "      </node>\n";
    }
    gexf << "    </nodes>\n";

    // Edges
    gexf << "    <edges>\n";
    size_t edge_id = 0;
    for (const auto& cell : cells) {
        if (cell.parent_id >= 0) {  // Ensure the parent exists
            gexf << "      <edge id=\"" << edge_id++
                 << "\" source=\"" << cell.parent_id
                 << "\" target=\"" << cell.id
                 << "\"/>\n";
        }
    }
    gexf << "    </edges>\n";

    // Footer
    gexf << "  </graph>\n";
    gexf << "</gexf>\n";

    gexf.close();
    spdlog::info("GEXF file written to {}", output_file);
}

void RunDataEngine::exportToCSV(const tbb::concurrent_vector<Cell>& cells, const std::string& output_file) {
    std::ofstream csv(output_file);
    if (!csv.is_open()) {
        spdlog::error("Failed to open file for CSV output: {}", output_file);
        return;
    }

    // Write the header
    csv << "id,parent_id,birth_time,death_time,state,fitness,mutations\n";

    // Write the data for each cell
    for (const auto& cell : cells) {
        csv << cell.id << ",";
        csv << (cell.parent_id >= 0 ? std::to_string(cell.parent_id) : "NA") << ",";
        csv << std::fixed << std::setprecision(5) << cell.birth_time << ",";
        csv << (cell.state == Cell::State::DEAD ? std::to_string(cell.death_time) : "NA") << ",";
        csv << (cell.state == Cell::State::ALIVE ? "ALIVE" : "DEAD") << ",";
        csv << cell.fitness << ",";
        
        // Serialize mutations as a semicolon-separated list
        if (!cell.mutations.empty()) {
            for (size_t i = 0; i < cell.mutations.size(); ++i) {
                csv << cell.mutations[i].type;
                if (i < cell.mutations.size() - 1) csv << ";";
            }
        } else {
            csv << "NA";
        }

        csv << "\n";
    }

    csv.close();
    spdlog::info("CSV file written to {}", output_file);
}

} // namespace core