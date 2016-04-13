#include <aikido/statespace/SE2StateSpace.hpp>
#include <Eigen/Geometry>

namespace aikido
{
namespace statespace
{
//=============================================================================
SE2StateSpace::State::State()
    : mTransform(Isometry2d::Identity())
{
}

//=============================================================================
SE2StateSpace::State::State(const Isometry2d& _transform)
    : mTransform(_transform)
{
}

//=============================================================================
auto SE2StateSpace::State::getIsometry() const -> const Isometry2d &
{
  return mTransform;
}

//=============================================================================
void SE2StateSpace::State::setIsometry(const Isometry2d& _transform)
{
  mTransform = _transform;
}

//=============================================================================
auto SE2StateSpace::createState() const -> ScopedState
{
  return ScopedState(this);
}

//=============================================================================
auto SE2StateSpace::getIsometry(const State* _state) const -> const Isometry2d &
{
  return _state->getIsometry();
}

//=============================================================================
void SE2StateSpace::setIsometry(State* _state,
                                const Isometry2d& _transform) const
{
  _state->setIsometry(_transform);
}

//=============================================================================
size_t SE2StateSpace::getStateSizeInBytes() const { return sizeof(State); }

//=============================================================================
StateSpace::State* SE2StateSpace::allocateStateInBuffer(void* _buffer) const
{
  return new (_buffer) State;
}

//=============================================================================
void SE2StateSpace::freeStateInBuffer(StateSpace::State* _state) const
{
  static_cast<State*>(_state)->~State();
}

//=============================================================================
void SE2StateSpace::compose(const StateSpace::State* _state1,
                            const StateSpace::State* _state2,
                            StateSpace::State* _out) const
{
  auto state1 = static_cast<const State*>(_state1);
  auto state2 = static_cast<const State*>(_state2);
  auto out = static_cast<State*>(_out);

  out->mTransform = state1->mTransform * state2->mTransform;
}

//=============================================================================
unsigned int SE2StateSpace::getDimension() const { return 3; }

//=============================================================================
void SE2StateSpace::getIdentity(StateSpace::State* _out) const
{
  auto out = static_cast<State*>(_out);
  setIsometry(out, Isometry2d::Identity());
}

//=============================================================================
void SE2StateSpace::getInverse(const StateSpace::State* _in,
                               StateSpace::State* _out) const
{
  auto in = static_cast<const State*>(_in);
  auto out = static_cast<State*>(_out);
  setIsometry(out, getIsometry(in).inverse());
}

//=============================================================================
void SE2StateSpace::copyState(StateSpace::State* _destination,
                              const StateSpace::State* _source) const
{
  auto source = static_cast<const State*>(_source);
  auto dest = static_cast<State*>(_destination);
  setIsometry(dest, getIsometry(source));
}

//=============================================================================
void SE2StateSpace::expMap(const Eigen::VectorXd& _tangent,
                           StateSpace::State* _out) const
{
  auto out = static_cast<State*>(_out);

  if (_tangent.rows() != 3) {
    std::stringstream msg;
    msg << "_tangent has incorrect size: expected 3"
        << ", got " << _tangent.rows() << ".\n";
    throw std::runtime_error(msg.str());
  }

  double angle = _tangent(0);
  Eigen::Vector2d translation = _tangent.bottomRows(2);

  Isometry2d transform(Isometry2d::Identity());
  transform.linear() = Eigen::Rotation2Dd(angle).matrix();
  transform.translation() = translation;

  out->mTransform = transform;
}

//=============================================================================
void SE2StateSpace::logMap(const StateSpace::State* _in,
                           Eigen::VectorXd& _tangent) const
{
  if (_tangent.rows() != 3) {
    std::stringstream msg;
    msg << "_tangent has incorrect size: expected 3"
        << ", got " << _tangent.rows() << ".\n";
    throw std::invalid_argument(msg.str());
  }

  auto in = static_cast<const State*>(_in);

  Isometry2d transform = getIsometry(in);
  _tangent.bottomRows(2) = transform.translation();
  Eigen::Rotation2Dd rotation = Eigen::Rotation2Dd::Identity();
  rotation.fromRotationMatrix(transform.rotation());
  _tangent(0) = rotation.angle();
}

}  // namespace statespace
}  // namespace aikido
