/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Detects a fade in/out (also to/from white) from the luma of consecutive frames.
//
// In a fade the new frame is the old one with a changed brightness: every pixel follows new = a * old + b with a
// small error, the mean brightness moves in one direction, and most pixels move with it (the median change follows
// the mean change). Scene cuts, camera motion and moving objects do not fit that. A change of the whole picture
// (for example auto exposure) and some dissolves do, because they are the same thing mathematically.
//
// No D3D here on purpose, so that it can be tested on its own.
class CFadeDetector
{
public:
	static constexpr unsigned WIDTH  = 128; // size of the small copy of the frame
	static constexpr unsigned HEIGHT = 72;
	static constexpr size_t   COUNT  = (size_t)WIDTH * HEIGHT;

	static constexpr float MIN_STD      = 0.012f;  // the picture needs some content (standard deviation of the luma)
	static constexpr float RES_RATIO    = 0.25f;   // allowed error of the fit relative to the picture contrast
	static constexpr float RES_ABS      = 0.004f;  // ... plus this much (noise)
	static constexpr float MIN_DELTA    = 0.0004f; // minimal change of the mean luma between two frames
	static constexpr float MIN_CHANGE   = 0.004f;  // minimal |a - 1| + |b|
	static constexpr float MEDIAN_RATIO = 0.3f;    // the median change must follow the mean change (global change)
	static constexpr float MEDIAN_ACTIVE = 0.004f; // ... counted over the pixels that are not black in both frames (letterbox bars)
	static constexpr size_t MEDIAN_MIN_COUNT = 1000; // fewer such pixels: there is no picture to judge
	static constexpr float QUAD_MIN_PRED = 0.0003f; // a quadrant counts when the fit predicts at least this mean change
	static constexpr float QUAD_LO       = 0.4f;    // the real mean change of a quadrant must be within
	static constexpr float QUAD_HI       = 1.8f;    // [LO, HI] times the predicted one
	static constexpr double DRATIO       = 3.0;     // the mean change may not jump by more than this factor between frames
	static constexpr int   NEED         = 4;       // consecutive matching frames to start
	static constexpr int   MAX_MISS     = 2;       // consecutive non matching frames to stop

	void Reset()
	{
		m_havePrev = false;
		m_count    = 0;
		m_sign     = 0;
		m_miss     = 0;
		m_active   = false;
		m_haveLastD = false;
	}

	// the next frame does not follow the previous one (a frame was not available)
	void BreakSequence()
	{
		m_havePrev  = false;
		m_haveLastD = false;
	}

	bool IsActive() const { return m_active; }

	// luma: COUNT values in the range 0..1 of the next frame. Returns true while a fade is active.
	// rateScale (0.5 .. 1): 1 for 24 fps and slower, smaller for faster video, where a fade moves less per frame.
	bool Update(const float* luma, double rateScale = 1.0)
	{
		if (m_cur.size() != COUNT) {
			m_cur.resize(COUNT);
			m_prev.resize(COUNT);
			m_diff.resize(COUNT);
		}
		std::copy(luma, luma + COUNT, m_cur.begin());

		if (m_havePrev) {
			const size_t n = COUNT;

			double mp = 0, mc = 0;
			for (size_t i = 0; i < n; i++) {
				mp += m_prev[i];
				mc += m_cur[i];
			}
			mp /= n;
			mc /= n;

			double vp = 0, vc = 0, cov = 0;
			for (size_t i = 0; i < n; i++) {
				const double dp = m_prev[i] - mp;
				const double dc = m_cur[i] - mc;
				vp  += dp * dp;
				vc  += dc * dc;
				cov += dp * dc;
			}
			vp  /= n;
			vc  /= n;
			cov /= n;
			const double sp = std::sqrt(vp);
			const double sc = std::sqrt(vc);

			double a = 1.0, b = 0.0, res = 1.0;
			if (vp > 1e-9) {
				a = cov / vp;
				b = mc - a * mp;
				double rs = 0;
				for (size_t i = 0; i < n; i++) {
					const double e = m_cur[i] - (a * m_prev[i] + b);
					rs += e * e;
				}
				res = std::sqrt(rs / n);
			}

			const double d = mc - mp;

			// the fit predicts the mean change of every quadrant; a fade over the whole picture matches it everywhere,
			// a fade of a part of the picture does not
			double sumP[4] = {}, sumC[4] = {};
			for (unsigned y = 0; y < HEIGHT; y++) {
				for (unsigned x = 0; x < WIDTH; x++) {
					const unsigned q = (y >= HEIGHT / 2 ? 2u : 0u) + (x >= WIDTH / 2 ? 1u : 0u);
					sumP[q] += m_prev[(size_t)y * WIDTH + x];
					sumC[q] += m_cur[(size_t)y * WIDTH + x];
				}
			}
			int valid = 0, good = 0;
			for (int q = 0; q < 4; q++) {
				const double mpq  = sumP[q] / (n / 4);
				const double dq   = sumC[q] / (n / 4) - mpq;
				const double pred = (a - 1.0) * mpq + b;
				if (std::fabs(pred) > QUAD_MIN_PRED) {
					valid++;
					const double ratio = dq / pred;
					if (ratio >= QUAD_LO && ratio <= QUAD_HI) {
						good++;
					}
				}
			}
			const bool quadOk = valid >= 2 && good * 4 >= valid * 3;

			// median change over the pixels that are not black in both frames: black bars and the black of a dark
			// picture do not change and would put the median at zero
			size_t cnt = 0;
			for (size_t i = 0; i < n; i++) {
				if (std::max(m_prev[i], m_cur[i]) > MEDIAN_ACTIVE) {
					m_diff[cnt++] = m_cur[i] - m_prev[i];
				}
			}
			double md = 0.0;
			if (cnt >= MEDIAN_MIN_COUNT) {
				const size_t mid = cnt / 2;
				std::nth_element(m_diff.begin(), m_diff.begin() + mid, m_diff.begin() + cnt);
				md = m_diff[mid];
			}

			bool step = sp > MIN_STD
				&& sc > MIN_STD * 0.7
				&& res < RES_RATIO * sc + RES_ABS
				&& std::fabs(d) > MIN_DELTA * rateScale
				&& std::fabs(a - 1.0) + std::fabs(b) > MIN_CHANGE * rateScale
				&& std::fabs(md) > MEDIAN_RATIO * std::fabs(d)
				&& quadOk;
			const int sign = d > 0 ? 1 : -1;

			// a fade changes the brightness at a steady pace: a sudden jump of the pace is something else
			if (step && m_count > 0 && m_haveLastD) {
				const double r = std::fabs(d) / std::max(std::fabs(m_lastD), 1e-9);
				if (!(r >= 1.0 / DRATIO && r <= DRATIO)) {
					step = false;
				}
			}

			if (step && (m_sign == 0 || sign == m_sign)) {
				m_count++;
				m_sign = sign;
				m_miss = 0;
				m_lastD = d;
				m_haveLastD = true;
			}
			else if (step) {
				// the brightness changes in the other direction now: the fade, if there was one, has ended
				m_count  = 1;
				m_sign   = sign;
				m_miss   = 0;
				m_lastD  = d;
				m_haveLastD = true;
				m_active = false;
			}
			else {
				m_miss++;
				if (m_miss >= MAX_MISS) {
					m_count  = 0;
					m_sign   = 0;
					m_active = false;
					m_haveLastD = false;
				}
			}
			if (m_count >= NEED) {
				m_active = true;
			}
		}

		m_prev.swap(m_cur);
		m_havePrev = true;

		return m_active;
	}

private:
	std::vector<float> m_prev;
	std::vector<float> m_cur;
	std::vector<float> m_diff;
	bool m_havePrev = false;
	int  m_count    = 0;
	int  m_sign     = 0;
	int  m_miss     = 0;
	bool m_active   = false;
	double m_lastD  = 0.0;
	bool m_haveLastD = false;
};
