#pragma once

#include "ecology/EcologyTypes.h"
#include <array>
#include <cstddef>

namespace longland
{
class EcologyEngine
{
public:
    std::size_t addSpecies(std::string_view name, const SpeciesTraits& traits, double population, double health);
    void addRelationship(std::size_t from, std::size_t to, RelationshipType type, double strength);
    void clear() noexcept;
    void step(const ResourceMap& resources, const GlobalControls& controls, double deltaSeconds) noexcept;

    std::size_t speciesCount() const noexcept { return speciesSize; }
    std::size_t edgeCount() const noexcept { return edgeSize; }
    const SpeciesState& species(std::size_t index) const noexcept { return states[index]; }
    const SpeciesTraits& traits(std::size_t index) const noexcept { return traitData[index]; }
    const RelationshipEdge& edge(std::size_t index) const noexcept { return edges[index]; }

private:
    void stepSubframe(const ResourceMap&, const GlobalControls&, double) noexcept;
    void sanitizeState(std::size_t index) noexcept;

    std::array<SpeciesState, maxSpecies> states {};
    std::array<SpeciesTraits, maxSpecies> traitData {};
    std::array<RelationshipEdge, maxEdges> edges {};
    std::size_t speciesSize {};
    std::size_t edgeSize {};
};
} // namespace longland

