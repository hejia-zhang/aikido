#include <r3/ompl/DARTGeometricStateSpace.h>
#include <r3/ompl/DARTGeometricStateValidityChecker.h>
#include <ompl/base/SpaceInformation.h>

using r3::ompl::DARTGeometricStateValidityChecker;

DARTGeometricStateValidityChecker::DARTGeometricStateValidityChecker(
        ::ompl::base::SpaceInformation *space_info)
    : ::ompl::base::StateValidityChecker(space_info)
    , state_space_(space_info->getStateSpace()->as<DARTGeometricStateSpace>())
{
}

DARTGeometricStateValidityChecker::DARTGeometricStateValidityChecker(
        ::ompl::base::SpaceInformationPtr const &space_info)
    : ::ompl::base::StateValidityChecker(space_info)
    , state_space_(space_info->getStateSpace()->as<DARTGeometricStateSpace>())
{
}

bool DARTGeometricStateValidityChecker::isValid(
        ::ompl::base::State const *state) const
{
    typedef DARTGeometricStateSpace::StateType DARTState;

    state_space_->SetState(state->as<DARTState>());
    return !state_space_->IsInCollision();
}