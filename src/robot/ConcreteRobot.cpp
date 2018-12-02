#include "aikido/robot/ConcreteRobot.hpp"
#include "aikido/constraint/TestableIntersection.hpp"
#include "aikido/planner/ConfigurationToConfiguration.hpp"
#include "aikido/planner/ConfigurationToConfigurationPlanner.hpp"
#include "aikido/planner/dart/ConfigurationToConfiguration.hpp"
#include "aikido/planner/dart/ConfigurationToConfiguration_to_ConfigurationToConfiguration.hpp"
#include "aikido/robot/util.hpp"
#include "aikido/statespace/StateSpace.hpp"

namespace aikido {
namespace robot {

using constraint::dart::CollisionFreePtr;
using constraint::dart::ConstCollisionFreePtr;
using constraint::dart::TSRPtr;
using constraint::TestablePtr;
using constraint::ConstTestablePtr;
using planner::ConfigurationToConfiguration;
using planner::ConfigurationToConfigurationPlannerPtr;
using planner::TrajectoryPostProcessor;
using planner::parabolic::ParabolicSmoother;
using planner::parabolic::ParabolicTimer;
using planner::dart::
    ConfigurationToConfiguration_to_ConfigurationToConfiguration;
using statespace::dart::MetaSkeletonStateSpace;
using statespace::dart::MetaSkeletonStateSpacePtr;
using statespace::dart::ConstMetaSkeletonStateSpacePtr;
using statespace::StateSpacePtr;
using statespace::StateSpace;
using trajectory::TrajectoryPtr;
using trajectory::Interpolated;
using trajectory::InterpolatedPtr;
using trajectory::Spline;
using trajectory::UniqueSplinePtr;

using dart::dynamics::BodyNodePtr;
using dart::dynamics::MetaSkeleton;
using dart::dynamics::MetaSkeletonPtr;

// TODO: Temporary constants for planning calls.
// These should be defined when we construct planner adapter classes
// static const double collisionResolution = 0.1;
static const double asymmetryTolerance = 1e-3;

namespace {

// TODO: These may not generalize to many robots.
Eigen::VectorXd getSymmetricLimits(
    const MetaSkeleton& metaSkeleton,
    const Eigen::VectorXd& lowerLimits,
    const Eigen::VectorXd& upperLimits,
    const std::string& limitName,
    double asymmetryTolerance)
{
  const auto numDofs = metaSkeleton.getNumDofs();
  assert(static_cast<std::size_t>(lowerLimits.size()) == numDofs);
  assert(static_cast<std::size_t>(upperLimits.size()) == numDofs);

  Eigen::VectorXd symmetricLimits(numDofs);
  for (std::size_t iDof = 0; iDof < numDofs; ++iDof)
  {
    symmetricLimits[iDof] = std::min(-lowerLimits[iDof], upperLimits[iDof]);
    if (std::abs(lowerLimits[iDof] + upperLimits[iDof]) > asymmetryTolerance)
    {
      dtwarn << "MetaSkeleton '" << metaSkeleton.getName()
             << "' has asymmetric " << limitName << " limits ["
             << lowerLimits[iDof] << ", " << upperLimits[iDof]
             << "] for DegreeOfFreedom '"
             << metaSkeleton.getDof(iDof)->getName() << "' (index: " << iDof
             << "). Using a conservative limit of" << symmetricLimits[iDof]
             << ".";
    }
  }
  return symmetricLimits;
}

Eigen::VectorXd getSymmetricVelocityLimits(
    const MetaSkeleton& metaSkeleton, double asymmetryTolerance)
{
  return getSymmetricLimits(
      metaSkeleton,
      metaSkeleton.getVelocityLowerLimits(),
      metaSkeleton.getVelocityUpperLimits(),
      "velocity",
      asymmetryTolerance);
}

Eigen::VectorXd getSymmetricAccelerationLimits(
    const MetaSkeleton& metaSkeleton, double asymmetryTolerance)
{
  return getSymmetricLimits(
      metaSkeleton,
      metaSkeleton.getAccelerationLowerLimits(),
      metaSkeleton.getAccelerationUpperLimits(),
      "acceleration",
      asymmetryTolerance);
}

} // namespace

//==============================================================================
ConcreteRobot::ConcreteRobot(
    const std::string& name,
    MetaSkeletonPtr metaSkeleton,
    bool /*simulation*/,
    common::UniqueRNGPtr rng,
    control::TrajectoryExecutorPtr trajectoryExecutor,
    dart::collision::CollisionDetectorPtr collisionDetector,
    std::shared_ptr<dart::collision::BodyNodeCollisionFilter>
        selfCollisionFilter)
  : mRootRobot(this)
  , mName(name)
  , mMetaSkeleton(metaSkeleton)
  , mStateSpace(std::make_shared<MetaSkeletonStateSpace>(mMetaSkeleton.get()))
  , mParentSkeleton(nullptr)
  // , mSimulation(simulation)
  , mRng(std::move(rng))
  , mTrajectoryExecutor(std::move(trajectoryExecutor))
  // , mCollisionResolution(collisionResolution)
  , mCollisionDetector(collisionDetector)
  , mSelfCollisionFilter(selfCollisionFilter)
{
  if (!mMetaSkeleton)
    throw std::invalid_argument("Robot is nullptr.");

  mParentSkeleton = mMetaSkeleton->getBodyNode(0)->getSkeleton();
}

//==============================================================================
UniqueSplinePtr ConcreteRobot::smoothPath(
    const dart::dynamics::MetaSkeletonPtr& metaSkeleton,
    const aikido::trajectory::Trajectory* path,
    const constraint::TestablePtr& constraint)
{
  Eigen::VectorXd velocityLimits = getVelocityLimits(*metaSkeleton);
  Eigen::VectorXd accelerationLimits = getAccelerationLimits(*metaSkeleton);
  auto smoother
      = std::make_shared<ParabolicSmoother>(velocityLimits, accelerationLimits);

  auto interpolated = dynamic_cast<const Interpolated*>(path);
  if (interpolated)
    return smoother->postprocess(
        *interpolated, *(cloneRNG().get()), constraint);

  auto spline = dynamic_cast<const Spline*>(path);
  if (spline)
    return smoother->postprocess(*spline, *(cloneRNG().get()), constraint);

  throw std::invalid_argument("Path should be either Spline or Interpolated.");
}

//==============================================================================
UniqueSplinePtr ConcreteRobot::retimePath(
    const dart::dynamics::MetaSkeletonPtr& metaSkeleton,
    const aikido::trajectory::Trajectory* path)
{
  Eigen::VectorXd velocityLimits = getVelocityLimits(*metaSkeleton);
  Eigen::VectorXd accelerationLimits = getAccelerationLimits(*metaSkeleton);
  auto retimer
      = std::make_shared<ParabolicTimer>(velocityLimits, accelerationLimits);

  auto interpolated = dynamic_cast<const Interpolated*>(path);
  if (interpolated)
    return retimer->postprocess(*interpolated, *(cloneRNG().get()));

  auto spline = dynamic_cast<const Spline*>(path);
  if (spline)
    return retimer->postprocess(*spline, *(cloneRNG().get()));

  throw std::invalid_argument("Path should be either Spline or Interpolated.");
}

//==============================================================================
std::future<void> ConcreteRobot::executeTrajectory(
    const TrajectoryPtr& trajectory) const
{
  return mTrajectoryExecutor->execute(trajectory);
}

//==============================================================================
boost::optional<Eigen::VectorXd> ConcreteRobot::getNamedConfiguration(
    const std::string& name) const
{
  auto configuration = mNamedConfigurations.find(name);
  if (configuration == mNamedConfigurations.end())
    return boost::none;

  return configuration->second;
}

//==============================================================================
void ConcreteRobot::setNamedConfigurations(
    std::unordered_map<std::string, const Eigen::VectorXd> namedConfigurations)
{
  mNamedConfigurations = std::move(namedConfigurations);
}

//==============================================================================
std::string ConcreteRobot::getName() const
{
  return mName;
}

//==============================================================================
dart::dynamics::ConstMetaSkeletonPtr ConcreteRobot::getMetaSkeleton() const
{
  return mMetaSkeleton;
}

//==============================================================================
statespace::dart::ConstMetaSkeletonStateSpacePtr ConcreteRobot::getStateSpace()
    const
{
  return mStateSpace;
}

//=============================================================================
void ConcreteRobot::setRoot(Robot* robot)
{
  if (robot == nullptr)
    throw std::invalid_argument("ConcreteRobot is null.");

  mRootRobot = robot;
}

//==============================================================================
void ConcreteRobot::step(const std::chrono::system_clock::time_point& timepoint)
{
  // Assumes that the parent robot is locked
  mTrajectoryExecutor->step(timepoint);
}

//==============================================================================
Eigen::VectorXd ConcreteRobot::getVelocityLimits(
    const MetaSkeleton& metaSkeleton) const
{
  return getSymmetricVelocityLimits(metaSkeleton, asymmetryTolerance);
}

//==============================================================================
Eigen::VectorXd ConcreteRobot::getAccelerationLimits(
    const MetaSkeleton& metaSkeleton) const
{
  return getSymmetricAccelerationLimits(metaSkeleton, asymmetryTolerance);
}

// ==============================================================================
CollisionFreePtr ConcreteRobot::getSelfCollisionConstraint(
    const ConstMetaSkeletonStateSpacePtr& space,
    const MetaSkeletonPtr& metaSkeleton) const
{
  using constraint::dart::CollisionFree;

  if (mRootRobot != this)
    return mRootRobot->getSelfCollisionConstraint(space, metaSkeleton);

  mParentSkeleton->enableSelfCollisionCheck();
  mParentSkeleton->disableAdjacentBodyCheck();

  // TODO: Switch to PRIMITIVE once this is fixed in DART.
  // mCollisionDetector->setPrimitiveShapeType(FCLCollisionDetector::PRIMITIVE);
  auto collisionOption
      = dart::collision::CollisionOption(false, 1, mSelfCollisionFilter);
  auto collisionFreeConstraint = std::make_shared<CollisionFree>(
      space, metaSkeleton, mCollisionDetector, collisionOption);
  collisionFreeConstraint->addSelfCheck(
      mCollisionDetector->createCollisionGroupAsSharedPtr(mMetaSkeleton.get()));
  return collisionFreeConstraint;
}

//=============================================================================
TestablePtr ConcreteRobot::getFullCollisionConstraint(
    const ConstMetaSkeletonStateSpacePtr& space,
    const MetaSkeletonPtr& metaSkeleton,
    const CollisionFreePtr& collisionFree) const
{
  using constraint::TestableIntersection;

  if (mRootRobot != this)
    return mRootRobot->getFullCollisionConstraint(
        space, metaSkeleton, collisionFree);

  auto selfCollisionFree = getSelfCollisionConstraint(space, metaSkeleton);

  if (!collisionFree)
    return selfCollisionFree;

  // Make testable constraints for collision check
  std::vector<ConstTestablePtr> constraints;
  constraints.reserve(2);
  constraints.emplace_back(selfCollisionFree);
  if (collisionFree)
  {
    if (collisionFree->getStateSpace() != space)
    {
      throw std::runtime_error("CollisionFree has incorrect statespace.");
    }
    constraints.emplace_back(collisionFree);
  }

  return std::make_shared<TestableIntersection>(space, constraints);
}

//==============================================================================
std::shared_ptr<TrajectoryPostProcessor>
ConcreteRobot::getTrajectoryPostProcessor(
    const dart::dynamics::MetaSkeletonPtr& metaSkeleton,
    bool enableShortcut,
    bool enableBlend,
    double shortcutTimelimit,
    double blendRadius,
    int blendIterations,
    double feasibilityCheckResolution,
    double feasibilityApproxTolerance) const
{
  Eigen::VectorXd velocityLimits = getVelocityLimits(*metaSkeleton);
  Eigen::VectorXd accelerationLimits = getAccelerationLimits(*metaSkeleton);

  return std::make_shared<ParabolicSmoother>(
      velocityLimits,
      accelerationLimits,
      enableShortcut,
      enableBlend,
      shortcutTimelimit,
      blendRadius,
      blendIterations,
      feasibilityCheckResolution,
      feasibilityApproxTolerance);
}

//==============================================================================
TrajectoryPtr ConcreteRobot::planToConfiguration(
    ConfigurationToConfigurationPlannerPtr planner,
    const MetaSkeletonPtr& metaSkeleton,
    ConstMetaSkeletonStateSpacePtr metaSkeletonStateSpace,
    const StateSpace::State* goalState,
    const CollisionFreePtr constraint)
{
  // TODO (avk): Take in base planner
  // Try to cast into single problem planner and do the following
  // Otherwise convert to composite planner, convert each of the under
  // lying planners to dart planners and then plan.
  // Move all this code to utils to keep this file cleaner.

  auto collisionConstraint = getFullCollisionConstraint(
      metaSkeletonStateSpace, metaSkeleton, constraint);

  // Get the states
  auto const start = metaSkeletonStateSpace->getScopedStateFromMetaSkeleton(
      metaSkeleton.get());
  auto const goal = metaSkeletonStateSpace->createState();
  metaSkeletonStateSpace->copyState(goalState, goal);

  // Create the problem
  const planner::dart::ConfigurationToConfiguration problem(
      metaSkeletonStateSpace, start, goal, collisionConstraint);

  // Convert the planner to a dart planner
  auto dartPlanner = std::
      make_shared<ConfigurationToConfiguration_to_ConfigurationToConfiguration>(
          planner, metaSkeleton);

  // Call plan on the problem
  auto trajectory = dartPlanner->plan(problem);

  if (!trajectory)
    return trajectory;

  return nullptr;
}

//=============================================================================
void ConcreteRobot::setCRRTPlannerParameters(
    const util::CRRTPlannerParameters& crrtParameters)
{
  mCRRTParameters = crrtParameters;
}

//==============================================================================
std::unique_ptr<common::RNG> ConcreteRobot::cloneRNG()
{
  return mRng->clone();
}

} // namespace robot
} // namespace aikido
