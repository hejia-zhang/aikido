#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <Eigen/Core>
#include <Eigen/QR>
#include <Eigen/StdVector>

#include <iostream>
#include <fstream>

template <class MatrixType>
struct IsFixedSizeVectorizable
{
  static constexpr bool IsSizeKnownAtCompileTime
    = (MatrixType::SizeAtCompileTime != Eigen::Dynamic);
  static constexpr bool SizeInBytesAtCompileTime
    = MatrixType::SizeAtCompileTime * sizeof(MatrixType::Scalar);
  static constexpr bool Value
    = (IsSizeKnownAtCompileTime && (SizeInBytesAtCompileTime % 4) == 0);
};

template <
  class _Scalar = double,
  class _Index = ptrdiff_t,
  _Index _NumCoefficients = Eigen::Dynamic,
  _Index _NumOutputs = Eigen::Dynamic,
  _Index _NumKnots = Eigen::Dynamic>
class SplineProblem
{
public:
  using Scalar = _Scalar;
  using Index = _Index;

  static constexpr Index NumCoefficientsAtCompileTime = _NumCoefficients;
  static constexpr Index NumOutputsAtCompileTime = _NumOutputs;
  static constexpr Index NumKnotsAtCompileTime= _NumKnots;
  static constexpr Index NumSegmentsAtCompileTime
    = (_NumKnots != Eigen::Dynamic)
      ? (NumKnotsAtCompileTime - 1)
      : Eigen::Dynamic;
  static constexpr Index DimensionAtCompileTime
    = (NumSegmentsAtCompileTime != Eigen::Dynamic && _NumCoefficients != Eigen::Dynamic)
      ? (NumSegmentsAtCompileTime * NumCoefficientsAtCompileTime)
      : Eigen::Dynamic;

  using TimeVector = Eigen::Matrix<Scalar, NumKnotsAtCompileTime, 1>;
  using OutputVector = Eigen::Matrix<Scalar, NumOutputsAtCompileTime, 1>;
  using OutputMatrix = Eigen::Matrix<Scalar, NumCoefficientsAtCompileTime, NumOutputsAtCompileTime>;
  using CoefficientVector = Eigen::Matrix<Scalar, NumCoefficientsAtCompileTime, 1>;
  using CoefficientMatrix = Eigen::Matrix<Scalar, NumCoefficientsAtCompileTime, NumCoefficientsAtCompileTime>;
  using ProblemMatrix = Eigen::Matrix<Scalar, DimensionAtCompileTime, DimensionAtCompileTime>;
  using ProblemVector = Eigen::Matrix<Scalar, DimensionAtCompileTime, NumOutputsAtCompileTime>;
  using SolutionMatrix = Eigen::Matrix<Scalar, NumOutputsAtCompileTime, NumCoefficientsAtCompileTime>;

  explicit SplineProblem(const TimeVector& _times);
  SplineProblem(const TimeVector& _times, Index _numCoefficients, Index _numOutputs);

  CoefficientVector createTimeVector(Scalar _t, Index _i) const;
  CoefficientMatrix createTimeMatrix(Scalar _t) const;
  CoefficientMatrix createCoefficientMatrix() const;

  void addConstantConstraint(Index _knot, Index _derivative, const OutputVector& _value);
  void addContinuityConstraint(Index _knot, Index _derivative);

  void fit();
  Index getSegmentIndex(Scalar _t) const;
  OutputVector interpolate(Scalar _t, Index _derivative) const;

//private:
  Index mNumKnots;
  Index mNumSegments;
  Index mNumCoefficients;
  Index mNumOutputs;
  Index mDimension;

  CoefficientMatrix mCoefficientMatrix;

  Index mRowIndex;
  TimeVector mTimes;
  ProblemMatrix mA;
  ProblemVector mB;

  std::vector<SolutionMatrix,
    Eigen::aligned_allocator<SolutionMatrix> > mSolution; // length _NumSegments

public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW_IF(
       IsFixedSizeVectorizable<CoefficientMatrix>::Value
    || IsFixedSizeVectorizable<TimeVector>::Value
    || IsFixedSizeVectorizable<ProblemMatrix>::Value
    || IsFixedSizeVectorizable<ProblemVector>::Value
  );
};

// ---

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::SplineProblem(const TimeVector& _times)
    : SplineProblem(_times, NumCoefficientsAtCompileTime, NumOutputsAtCompileTime)
{
  static_assert(NumCoefficientsAtCompileTime != Eigen::Dynamic,
    "NumCoefficientsAtCompileTime must be static to use this constructor.");
  static_assert(NumOutputsAtCompileTime != Eigen::Dynamic,
    "NumOutputsAtCompileTime must be static to use this constructor.");
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::SplineProblem(const TimeVector& _times, Index _numCoefficients, Index _numOutputs)
    : mNumKnots(_times.size()),
      mNumSegments(std::max<Index>(mNumKnots - 1, 0)),
      mNumCoefficients(_numCoefficients),
      mNumOutputs(_numOutputs),
      mDimension(mNumSegments * _numCoefficients),
      mCoefficientMatrix(createCoefficientMatrix()),
      mRowIndex(0),
      mTimes(_times),
      mA(mDimension, mDimension),
      mB(mDimension, _numOutputs),
      mSolution(mNumSegments, SolutionMatrix(_numOutputs, _numCoefficients))
{
  mA.setZero();
  mB.setZero();

  std::cout << "\n\n"
            << "mNumKnots = " << mNumKnots << "\n"
            << "mNumSegments = " << mNumSegments << "\n"
            << "mNumCoefficients = " << mNumCoefficients << "\n"
            << "mNumOutputs = " << mNumOutputs << "\n"
            << "mDimension = " << mDimension << "\n"
            << "\n\n"
            << std::flush;

  if (!std::is_sorted(mTimes.data(), mTimes.data() + mTimes.size())) {
    throw std::runtime_error("Times are not monotonically increasing.");
  }
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
void SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::addConstantConstraint(
    Index _knot, Index _derivative, const OutputVector& _value)
{
  assert(0 <= _knot && _knot < mNumKnots);
  assert(0 <= _derivative && _derivative < mNumCoefficients);
  assert(_value.size() == mNumOutputs);

  const CoefficientVector timeVector = createTimeVector(mTimes[_knot], _derivative);
  const CoefficientVector derivativeVector = mCoefficientMatrix.row(_derivative);
  const CoefficientVector coeffVector = derivativeVector.cwiseProduct(timeVector);

  // Position constraint on segment before this knot.
  if (_knot > 0) {
    assert(mRowIndex < mDimension);

    mA.block(mRowIndex, (_knot - 1) * mNumCoefficients, 1, mNumCoefficients)
      = coeffVector.transpose();
    mB.row(mRowIndex) = _value;

    ++mRowIndex;
  }

  // Position constraint on segment after this knot.
  if (_knot + 1 < mNumKnots) {
    assert(mRowIndex < mDimension);

    mA.block(mRowIndex, _knot * mNumCoefficients, 1, mNumCoefficients)
      = coeffVector.transpose();
    mB.row(mRowIndex) = _value;

    ++mRowIndex;
  }
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
void SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::addContinuityConstraint(Index _knot, Index _derivative)
{
  assert(0 <= _knot && _knot < mNumKnots);
  assert(_knot != 0 && _knot + 1 != mNumKnots);
  assert(0 <= _derivative && _derivative < mNumCoefficients);
  assert(mRowIndex < mDimension);

  const CoefficientVector derivativeVector = mCoefficientMatrix.row(_derivative);
  const CoefficientVector timeVector = createTimeVector(mTimes[_knot], _derivative);
  const CoefficientVector coeffVector = derivativeVector.cwiseProduct(timeVector);
  
  mA.block(mRowIndex, (_knot - 1) * mNumCoefficients, 1, mNumCoefficients)
    = coeffVector.transpose();
  mA.block(mRowIndex,  _knot      * mNumCoefficients, 1, mNumCoefficients)
    = -coeffVector.transpose();
  mB.row(mRowIndex).setZero();

  ++mRowIndex;
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
auto SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::createTimeVector(Scalar _t, Index _i) const -> CoefficientVector
{
  CoefficientVector exponents(mNumCoefficients);

  for (Index j = 0; j < mNumCoefficients; ++j) {
    if (j > _i) {
      exponents[j] = std::pow(_t, j - _i);
    } else if (j == _i) {
      exponents[j] = 1;
    } else {
      exponents[j] = 0;
    }
  }

  return exponents;
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
void SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::fit()
{
  std::cout << "!!! " << mRowIndex << " ?= " << mDimension << std::endl;
  assert(mRowIndex == mDimension);

  using MatrixType = Eigen::Matrix<Scalar, DimensionAtCompileTime, DimensionAtCompileTime>;

  // Perform the QR decomposition once. 
  Eigen::HouseholderQR<MatrixType> solver = mA.householderQr();

  for (Index ioutput = 0; ioutput < mNumOutputs; ++ioutput) {
    // Solve for the spline coefficients for each output dimension.
    Eigen::Matrix<Scalar, DimensionAtCompileTime, 1> solutionVector
      = solver.solve(mB.col(ioutput));

    // Split the coefficients by segment.
    for (Index isegment = 0; isegment < mNumSegments; ++isegment) {
      SolutionMatrix& solutionMatrix = mSolution[isegment];
      solutionMatrix.row(ioutput) = solutionVector.segment(
        isegment * mNumCoefficients, mNumCoefficients);
    }
  }
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
auto SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::createCoefficientMatrix() const -> CoefficientMatrix
{
  CoefficientMatrix coefficients(mNumCoefficients, mNumCoefficients);
  coefficients.setZero();

  if (mNumCoefficients > 0) {
    coefficients.row(0).setOnes();
  }

  for (Index i = 1; i < mNumCoefficients; ++i) {
    for (Index j = i; j < mNumCoefficients; ++j) {
      coefficients(i, j) = (j - i + 1) * coefficients(i - 1, j);
    }
  }
  return coefficients;
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
auto SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::getSegmentIndex(Scalar _t) const -> Index
{
  if (_t <= mTimes[0]) {
    return 0;
  } else if (_t >= mTimes[mNumKnots - 1]) {
    return mNumSegments - 1;
  } else {
    auto it = std::lower_bound(mTimes.data(), mTimes.data() + mTimes.size(), _t);
    return it - mTimes.data() - 1;
  }
}

template <
  class Scalar, class Index,
  Index _NumCoefficients, Index _NumOutputs, Index _NumKnots>
auto SplineProblem<Scalar, Index, _NumCoefficients, _NumOutputs, _NumKnots>
  ::interpolate(Scalar _t, Index _derivative) const -> OutputVector
{
  const CoefficientVector timeVector = createTimeVector(_t, _derivative);
  const CoefficientVector derivativeVector = mCoefficientMatrix.row(_derivative);
  const CoefficientVector evaluationVector = derivativeVector.cwiseProduct(timeVector);
  const Index segmentIndex = getSegmentIndex(_t);
  const SolutionMatrix& solutionMatrix = mSolution[segmentIndex];

  OutputVector output(mNumOutputs);

  for (Index ioutput = 0; ioutput < mNumOutputs; ++ioutput) {
    const CoefficientVector solutionVector = solutionMatrix.row(ioutput);
    output[ioutput] = evaluationVector.dot(solutionVector);
  }

  return output;
}

// ---

int main(int argc, char **argv)
{
  using Eigen::VectorXd;

  using Vector1d = Eigen::Matrix<double, 1, 1>;

  auto Value = [](double x, double y) {
    Eigen::Matrix<double, 2, 1> v;
    v << x, y;
    return v;
  };

  VectorXd times(3);
  times << 0, 1, 3;

  //SplineProblem<double, ptrdiff_t, 4, 2, 3> problem(times, 4, 2);
  SplineProblem<double, ptrdiff_t, 4, 2, 3> problem(times);
  problem.addConstantConstraint(0, 1, Value(0, 0));
  problem.addConstantConstraint(0, 0, Value(5, 7));
  problem.addConstantConstraint(1, 0, Value(6, 8));
  problem.addContinuityConstraint(1, 1);
  problem.addContinuityConstraint(1, 2);
  problem.addConstantConstraint(2, 0, Value(0, 2));
  problem.addConstantConstraint(2, 1, Value(0, 0));
  problem.fit();

  std::cout << "A =\n" << problem.mA << "\n\n";
  std::cout << "b =\n" << problem.mB.transpose() << "\n\n";
  for (int i = 0; i < problem.mSolution.size(); ++i) {
    std::cout << "x =\n" << problem.mSolution[i] << "\n\n";
  }


  std::ofstream csv("/tmp/data.csv", std::ios::binary);
  std::cout << "\n\n";
  for (double t = times[0]; t <= times[times.size() - 1] + 1e-3; t += 0.05) {
    csv << t << '\t' << problem.interpolate(t, 0).transpose() << '\t' << problem.getSegmentIndex(t) << '\n';
  }

  return 0;
}
