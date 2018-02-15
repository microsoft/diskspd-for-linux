// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <map>
#include <unordered_map>
#include <string>
#include <limits>
#include <cmath>

#ifndef DISKSPD_HISTOGRAM_H
#define DISKSPD_HISTOGRAM_H

namespace diskspd {

template<typename T>
class Histogram
{
	private:

	unsigned _samples;

#define DISKSPD_HISTOGRAM_USE_HASH_TABLE
#ifdef DISKSPD_HISTOGRAM_USE_HASH_TABLE
	std::unordered_map<T,unsigned> _data;

	std::map<T,unsigned> _GetSortedData() const
	{
		return std::map<T,unsigned>(_data.begin(), _data.end());
	}
#else
	std::map<T,unsigned> _data;

	std::map<T,unsigned> _GetSortedData() const
	{
		return _data;
	}
#endif
	public:

	Histogram()
		: _samples(0)
	{}

	void Clear()
	{
		_data.clear();
		_samples = 0;
	}

	void Add(T v)
	{
		_data[ v ]++;
		_samples++;
	}

	void Merge(const Histogram<T> &other)
	{
		for (auto i : other._data)
		{
			_data[ i.first ] += i.second;
		}

		_samples += other._samples;
	}

	T GetMin() const
	{
		T min(std::numeric_limits<T>::max());

		for (auto i : _data)
		{
			if (i.first < min)
			{
				min = i.first;
			}
		}

		return min;
	}

	T GetMax() const
	{
		T max(std::numeric_limits<T>::min());

		for (auto i : _data)
		{
			if (i.first > max)
			{
				max = i.first;
			}
		}

		return max;
	}

	unsigned GetSampleSize() const
	{
		return _samples;
	}

	T GetPercentile(double p) const
	{
		// ISSUE-REVIEW
		// What do the 0th and 100th percentile really mean?
		if ((p < 0) || (p > 1))
		{
			throw std::invalid_argument("Percentile must be >= 0 and <= 1");
		}

		const double target = GetSampleSize() * p;

		unsigned cur = 0;
		for (auto i : _GetSortedData())
		{
			cur += i.second;
			if (cur >= target)
			{
				return i.first;
			}
		}

		throw std::runtime_error("Percentile is undefined");
	}

	T GetPercentile(int p) const
	{
		return GetPercentile(static_cast<double>(p)/100);
	}

	T GetMedian() const
	{
		return GetPercentile(0.5);
	}

	double GetStdDev() const { return GetStandardDeviation(); }
	double GetAvg() const { return GetMean(); }

	double GetMean() const
	{
		double sum(0);
		unsigned samples = GetSampleSize();

		for (auto i : _data)
		{
			double bucket_val =
				static_cast<double>(i.first) * i.second / samples;

			if (sum + bucket_val < 0)
			{
				throw std::overflow_error("while trying to accumulate sum");
			}

			sum += bucket_val;
		}

		return sum;
	}

	double GetStandardDeviation() const
	{
		double mean(GetMean());
		double ssd(0);

		for (auto i : _data)
		{
			double dev = static_cast<double>(i.first) - mean;
			double sqdev = dev*dev;
			ssd += i.second * sqdev;
		}

		return sqrt(ssd / GetSampleSize());
	}

};

} // namespace diskspd

#endif // DISKSPD_HISTOGRAM_H
