#include "sif/bicyclecost.h"
#include <valhalla/baldr/directededge.h>
#include <valhalla/baldr/nodeinfo.h>

using namespace valhalla::baldr;

namespace valhalla {
namespace sif {

// Cost of traversing an edge with steps. Make this high but not impassible.
// Equal to about 0.5km.
const Cost kBicycleStepsCost = { 500.0f, 30.0f };

// How much to favor bicycle networks
constexpr float kBicycleNetworkFactor = 0.8f;

/**
 * Derived class providing dynamic edge costing for bicycle routes.
 */
class BicycleCost : public DynamicCost {
 public:
  /**
   * Constructor. Configuration / options for bicycle costing are provided
   * via a property tree.
   * @param  config  Property tree with configuration/options.
   */
  BicycleCost(const boost::property_tree::ptree& config);

  virtual ~BicycleCost();

  /**
   * Checks if access is allowed for the provided directed edge.
   * This is generally based on mode of travel and the access modes
   * allowed on the edge. However, it can be extended to exclude access
   * based on other parameters.
   * @param  edge  Pointer to a directed edge.
   * @param  pred  Predecessor edge information.
   * @return  Returns true if access is allowed, false if not.
   */
  virtual bool Allowed(const baldr::DirectedEdge* edge,
                       const EdgeLabel& pred) const;

  /**
   * Checks if access is allowed for the provided node. Node access can
   * be restricted if bollards or gates are present. (TODO - others?)
   * @param  edge  Pointer to node information.
   * @return  Returns true if access is allowed, false if not.
   */
  virtual bool Allowed(const baldr::NodeInfo* node) const;

  /**
   * Get the cost to traverse the specified directed edge. Cost includes
   * the time (seconds) to traverse the edge.
   * @param   edge  Pointer to a directed edge.
   * @param   density  Relative road density.
   * @return  Returns the cost and time (seconds)
   */
  virtual Cost EdgeCost(const baldr::DirectedEdge* edge,
                        const uint32_t density) const;

  /**
   * Returns the cost to make the transition from the predecessor edge.
   * Defaults to 0. Costing models that wish to include edge transition
   * costs (i.e., intersection/turn costs) must override this method.
   * @param  edge  Directed edge (the to edge)
   * @param  node  Node (intersection) where transition occurs.
   * @param  pred  Predecessor edge information.
   * @return  Returns the cost and time (seconds)
   */
// TODO - add logic for transition costs!
//  virtual Cost TransitionCost(const baldr::DirectedEdge* edge,
//                               const baldr::NodeInfo* node,
//                               const EdgeLabel& pred) const;

  /**
   * Get the cost factor for A* heuristics. This factor is multiplied
   * with the distance to the destination to produce an estimate of the
   * minimum cost to the destination. The A* heuristic must underestimate the
   * cost to the destination. So a time based estimate based on speed should
   * assume the maximum speed is used to the destination such that the time
   * estimate is less than the least possible time along roads.
   */
  virtual float AStarCostFactor() const;

 protected:
  enum class BicycleType {
    kRoad     = 0,
    kCross    = 1,
    kHybrid   = 2,
    kMountain = 3
  };

  // Bicycling speed (default to 25 kph)
  float speed_;

  // Bicycle type
  BicycleType bicycletype_;

  // Weighting applied for the smoothest surfaces. Weighting for other
  // surfaces starts at this factor and adds the bicycle surface factor
  // times the surface smoothness.
  float smooth_surface_factor_;

  // Bicycle surface factor. An indication of how much the bicycle type
  // influences selection of surface types. Road bikes may be more
  // influenced by surface type than other bikes. Negative values are
  // allowed if one wants to favor rougher surfaces, but keep the value to
  // no more than -0.1
  // TODO - elaborate more on how the weighting occurs

  float bicycle_surface_factor_;

  // A measure of willingness to ride with traffic. Ranges from 0-1 with
  // 0 being not willing at all and 1 being totally comfortable. This factor
  // determines how much cycle lanes and paths are preferred over roads (if
  // at all). Experienced road riders and messengers may use a value = 1
  // while beginners may use a value of 0.1 to stay away from roads unless
  // absolutely necessary.
  float useroads_;

 public:
  /**
   * Returns a function/functor to be used in location searching which will
   * exclude results from the search by looking at each edges attribution
   * @return Function to be used in filtering out edges
   */
  virtual const EdgeFilter GetFilter() const {
    //throw back a lambda that checks the access for this type of costing
    BicycleType b = bicycletype_;
    return [b](const baldr::DirectedEdge* edge){
      // Prohibit certain roads based on surface type and bicycle type
      if(!edge->trans_up() && !edge->trans_down() &&
        (edge->forwardaccess() & kBicycleAccess)) {
        if (b == BicycleType::kRoad)
          return edge->surface() > Surface::kPavedRough;
        else if (b == BicycleType::kHybrid)
          return edge->surface() > Surface::kCompacted;
        else if (b == BicycleType::kCross)
          return edge->surface() > Surface::kDirt;
        else if (b == BicycleType::kMountain)
          return edge->surface() >= Surface::kPath;
      }
      return true;
    };
  }
};

// Bicycle route costs are distance based with some favor/avoid based on
// attribution.
// TODO - add options and config settings.
// TODO - how to handle time/speed for estimating time on path

// Constructor
BicycleCost::BicycleCost(const boost::property_tree::ptree& config)
    : DynamicCost(config, TravelMode::kBicycle),
      speed_(25.0f),
      bicycletype_(BicycleType::kRoad),
      smooth_surface_factor_(0.9f),
      bicycle_surface_factor_(0.2f),
      useroads_(0.5f) {
}

// Destructor
BicycleCost::~BicycleCost() {
}

// Check if access is allowed on the specified edge.
bool BicycleCost::Allowed(const baldr::DirectedEdge* edge,
                          const EdgeLabel& pred) const {
  // Check bicycle access and turn restrictions. Bicycles should obey
  // vehicular turn restrictions. Disallow Uturns. Do not allow entering
  // not-thru edges except near the destination. Skip impassable edges.
  if (!(edge->forwardaccess() & kBicycleAccess) ||
      (pred.opp_local_idx() == edge->localedgeidx()) ||
      (pred.restrictions() & (1 << edge->localedgeidx())) ||
      (edge->not_thru() && pred.distance() > not_thru_distance_) ||
       edge->surface() == Surface::kImpassable) {
    return false;
  }

  // Prohibit certain roads based on surface type and bicycle type
  if (bicycletype_ == BicycleType::kRoad)
    return edge->surface() <= Surface::kPavedRough;
  else if (bicycletype_ == BicycleType::kHybrid)
      return edge->surface() <= Surface::kCompacted;
  else if (bicycletype_ == BicycleType::kCross)
    return edge->surface() <= Surface::kDirt;
  else if (bicycletype_ == BicycleType::kMountain)
    return edge->surface() <= Surface::kPath;   // Allow all but impassable

  return true;
}

// Check if access is allowed at the specified node.
bool BicycleCost::Allowed(const baldr::NodeInfo* node) const {
  return (node->access() & kBicycleAccess);
}

// Returns the cost to traverse the edge and an estimate of the actual time
// (in seconds) to traverse the edge.
Cost BicycleCost::EdgeCost(const baldr::DirectedEdge* edge,
                           const uint32_t density) const {
  // Stairs/steps - use a high fixed cost so they are generally avoided.
   if (edge->use() == Use::kSteps) {
     return kBicycleStepsCost;
   }

   // Alter cost based on desirability of cycling on this edge.
   // Based on several factors: type of bike, rider propensity
   // to ride on roads, surface, use, and presence of bike lane
   // or part of bike network
   float factor = 1.0f;

   // Surface factor
   float surface_factor = smooth_surface_factor_ + (bicycle_surface_factor_ *
           static_cast<uint32_t>(edge->surface()));
   factor *= surface_factor;

   // Favor dedicated cycleways (favor more if rider for riders not wanting
   // to use roadways! Footways with cycle access - favor unless the rider
   // wants to favor use of roads
   if (edge->use() == Use::kCycleway) {
     factor *= 0.65f;
   } else if (edge->use() == Use::kFootway) {
     // Cyclists who favor using roads want to avoid footways. A value of
     // 0.5 for useroads_ will result in no difference using footway
     factor *= 0.75f + (useroads_ * 0.5f);
   } else {
     if (edge->cyclelane() == CycleLane::kNone) {
       // No cycle lane exists, base the factor on auto speed on the road.
       // Roads < 65 kph are neutral. Start avoiding based on speed above this.
       // TODO - may want to avoid very low speed edges, alleys/service?
       float speedfactor = (edge->speed() < 65.0f) ? 1.0f :
             (1.0f + (edge->speed() - 65.0f) * (1.0f - useroads_) * 0.05f);
       factor *= speedfactor;
     } else {
       // A cycle lane exists. Favor separated lanes more than dedicated
       // and shared use.
       float laneusefactor = 0.75f + useroads_ * 0.05f;
       factor *= (laneusefactor + static_cast<uint32_t>(edge->cyclelane()) * 0.1f);
     }
   }

   // Slightly favor bicycle networks.
   // TODO - do we need to differentiate between types of network?
   if (edge->bikenetwork() > 0) {
     factor *= kBicycleNetworkFactor;
   }
   return Cost(edge->length() * factor, edge->length() / speed_);
}

/**
 * Get the cost factor for A* heuristics. This factor is multiplied
 * with the distance to the destination to produce an estimate of the
 * minimum cost to the destination. The A* heuristic must underestimate the
 * cost to the destination. So a time based estimate based on speed should
 * assume the maximum speed is used to the destination such that the time
 * estimate is less than the least possible time along roads.
 */
float BicycleCost::AStarCostFactor() const {
  // TODO - compute the most favorable weighting possible
  return 0.0f;
}

cost_ptr_t CreateBicycleCost(const boost::property_tree::ptree& config) {
  return std::make_shared<BicycleCost>(config);
}

}
}
