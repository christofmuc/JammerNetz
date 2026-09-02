#include "RunningStats.h"

#include <math.h>

RunningStats::RunningStats()
{
	Clear();
}

void RunningStats::Clear()
{
	n = 0;
	M1 = M2 = M3 = M4 = 0.0;
}

void RunningStats::Push(double x)
{
	double delta, delta_n, delta_n2, term1;

	const double previousCount = static_cast<double>(n);
	n++;
	const double count = static_cast<double>(n);
	delta = x - M1;
	delta_n = delta / count;
	delta_n2 = delta_n * delta_n;
	term1 = delta * delta_n * previousCount;
	M1 += delta_n;
	M4 += term1 * delta_n2 * (count * count - 3.0 * count + 3.0) + 6.0 * delta_n2 * M2 - 4.0 * delta_n * M3;
	M3 += term1 * delta_n * (count - 2.0) - 3.0 * delta_n * M2;
	M2 += term1;
}

long long RunningStats::NumDataValues() const
{
	return n;
}

double RunningStats::Mean() const
{
	return M1;
}

double RunningStats::Variance() const
{
	return M2 / (static_cast<double>(n) - 1.0);
}

double RunningStats::StandardDeviation() const
{
	return sqrt(Variance());
}

double RunningStats::Skewness() const
{
	return sqrt(static_cast<double>(n)) * M3 / pow(M2, 1.5);
}

double RunningStats::Kurtosis() const
{
	return static_cast<double>(n) * M4 / (M2 * M2) - 3.0;
}

RunningStats operator+(const RunningStats a, const RunningStats b)
{
	RunningStats combined;

	combined.n = a.n + b.n;
	const double aCount = static_cast<double>(a.n);
	const double bCount = static_cast<double>(b.n);
	const double combinedCount = static_cast<double>(combined.n);

	double delta = b.M1 - a.M1;
	double delta2 = delta * delta;
	double delta3 = delta * delta2;
	double delta4 = delta2 * delta2;

	combined.M1 = (aCount * a.M1 + bCount * b.M1) / combinedCount;

	combined.M2 = a.M2 + b.M2 +
		delta2 * aCount * bCount / combinedCount;

	combined.M3 = a.M3 + b.M3 +
		delta3 * aCount * bCount * (aCount - bCount) / (combinedCount * combinedCount);
	combined.M3 += 3.0 * delta * (aCount * b.M2 - bCount * a.M2) / combinedCount;

	combined.M4 = a.M4 + b.M4 + delta4 * aCount * bCount * (aCount * aCount - aCount * bCount + bCount * bCount) /
		(combinedCount * combinedCount * combinedCount);
	combined.M4 += 6.0 * delta2 * (aCount * aCount * b.M2 + bCount * bCount * a.M2) / (combinedCount * combinedCount) +
		4.0 * delta * (aCount * b.M3 - bCount * a.M3) / combinedCount;

	return combined;
}

RunningStats& RunningStats::operator+=(const RunningStats& rhs)
{
	RunningStats combined = *this + rhs;
	*this = combined;
	return *this;
}
