struct Position {
    float x, y, z;
};

struct Metabolism {
    float energy;
    float metabolicRate;
};

struct Genome {
    std::vector<uint8_t> dna;
};
