#ifndef __ARZ_IMPL_HPP__
#define __ARZ_IMPL_HPP__

#include <limits>
#include <cstring>

// arz implementation
template <typename T>
inline T arz<T>::epsilon()
{
    return std::numeric_limits<T>::epsilon();
}

// q implemenation
template <typename T>
arz<T>::q::q()
    : base()
{}

template <typename T>
arz<T>::q::q(const q &__restrict__ o)
    : base(o)
{}

template <typename T>
arz<T>::q::q(const base &__restrict__ o)
    : base(o)
{}

template <typename T>
template <class E>
arz<T>::q::q(const tvmet::XprVector< E, 2 > &__restrict__ e)
    : base(e)
{}

template <typename T>
arz<T>::q::q(const T in_rho, const T in_y)
    : base(in_rho, in_y)
{}

template <typename T>
arz<T>::q::q(const T in_rho, const T in_u,
             const T u_max,
             const T gamma)
    : base(in_rho, eq::y(in_rho, in_u, u_max, gamma))
{}

template <typename T>
inline const T arz<T>::q::rho() const
{
    return (*this)[0];
}

template <typename T>
inline T& arz<T>::q::rho()
{
    return (*this)[0];
}

template <typename T>
inline const T arz<T>::q::y() const
{
    return (*this)[1];
}

template <typename T>
inline T& arz<T>::q::y()
{
    return (*this)[1];
}

template <typename T>
inline void arz<T>::q::fix()
{
    if(rho() < epsilon())
        rho() = epsilon();
    if(y() > -epsilon())
        y() = -epsilon();
    assert(check());
}

template <typename T>
inline bool arz<T>::q::check() const
{
    return std::isfinite(rho()) && rho() >= 0 && rho() <= 1.0f
    && std::isfinite(y()) && y() <= 0;
}

// full_q implemenation
template <typename T>
arz<T>::full_q::full_q()
    : base()
{}

template <typename T>
arz<T>::full_q::full_q(const full_q &__restrict__ o)
    : base(o),
      u_eq_(o.u_eq_),
      u_(o.u_)
{
    assert(check());
}

template <typename T>
arz<T>::full_q::full_q(const q &__restrict__ o,
                       const T               u_max,
                       const T               gamma)
    : base(o)
{
    u_eq_ = eq::u_eq(base::rho(), u_max, gamma);
    u_    = base::rho() < epsilon() ? u_max :
            std::max(base::y()/base::rho() + u_eq_,
                     static_cast<T>(0));
    assert(check());
}

template <typename T>
arz<T>::full_q::full_q(const T in_rho, const T in_u,
                       const T u_max,
                       const T gamma)
{
    base::rho() = in_rho;
    u_eq_       = eq::u_eq(base::rho(), u_max, gamma);
    u_          = in_u;
    base::y()   = base::rho()*(u() - u_eq_);
    assert(check());
}

template <typename T>
inline void arz<T>::full_q::clear()
{
    memset(this, 0, sizeof(*this));
}

template <typename T>
inline const T& arz<T>::full_q::u() const
{
    return u_;
}

template <typename T>
inline T& arz<T>::full_q::u()
{
    return u_;
}

template <typename T>
inline const T& arz<T>::full_q::u_eq() const
{
    return u_eq_;
}

template <typename T>
inline T& arz<T>::full_q::u_eq()
{
    return u_eq_;
}

template <typename T>
inline tvmet::XprVector<
    tvmet::XprBinOp<
        tvmet::Fcnl_mul<T, T>,
        tvmet::VectorConstReference<T, 2>,
        tvmet::XprLiteral<T>
        >,
    2 > arz<T>::full_q::flux() const
{
    return (*this)*u();
}

template <typename T>
inline T arz<T>::full_q::flux_0() const
{
    return base::rho()*u();
}

template <typename T>
inline T arz<T>::full_q::flux_1() const
{
    return base::y()*u();
}

template <typename T>
inline T arz<T>::full_q::lambda_0(const T u_max, const T gamma) const
{
    return u() + base::rho()*eq::u_eq_prime(base::rho(), u_max, gamma);
}

template <typename T>
inline T arz<T>::full_q::lambda_1(const T u_max, const T gamma) const
{
    return u();
}

template <typename T>
inline typename arz<T>::full_q arz<T>::centered_rarefaction(const full_q &__restrict__ q_l,
                                                            const T                    u_max,
                                                            const T                    gamma,
                                                            const T                    inv_gamma)
{
    full_q res;

    res.u()    = gamma*(q_l.u() + u_max - q_l.u_eq())/(gamma+1);
    res.u_eq() = u_max - res.u()/(u_max*gamma);
    res.rho()  = std::pow( u_max - res.u_eq(), inv_gamma);
    res.y()    = res.rho()*(res.u() - res.u_eq());

    return res;
}

template <typename T>
inline typename arz<T>::full_q arz<T>::rho_middle(const full_q &__restrict__ q_l,
                                                  const full_q &__restrict__ q_r,
                                                  const T                    inv_u_max,
                                                  const T                    inv_gamma)
{
    full_q res;

    res.u_eq() = q_r.u() - q_l.u() + q_l.u_eq();
    res.rho()  = std::pow(1.0 - res.u_eq()*inv_u_max, inv_gamma);
    res.u()    = q_r.u();
    res.y()    = res.rho()*(res.u() - res.u_eq());

    return res;
}

template <typename T>
inline bool arz<T>::full_q::check() const
{
    return q::check() && std::isfinite(u()) && u() >= 0.0f
    && std::isfinite(u_eq()) && u_eq() >= 0.0f;
}

template <typename T>
inline void arz<T>::riemann_solution::riemann(const full_q &__restrict__ q_l,
                                              const full_q &__restrict__ q_r,
                                              const T                    u_max,
                                              const T                    inv_u_max,
                                              const T                    gamma,
                                              const T                    inv_gamma)
{
    const full_q *q_0;
    full_q        q_m;

    if(q_l.rho() < VACUUM_EPS)
    {   // case 4
        speeds[0] = 0.0;
        waves [0] = q(0.0, 0.0);

        speeds[1] = q_l.u();
        waves [1] = q_l;

        q_m.clear();
        q_0 = &q_m;
    }
    else if(q_r.rho() < VACUUM_EPS)
    {   // case 5
        q_m = full_q(0.0, q_l.u() + (u_max - q_l.u_eq()),
                     u_max, gamma);

        const T lambda_0_l = q_l.lambda_0(u_max, gamma);
        const T lambda_0_m = q_m.u();

        speeds[0] = (lambda_0_l + lambda_0_m)/2;
        waves [0] = q_m - q_l;

        speeds[1] = speeds[0];
        waves [1] = q(0.0, 0.0);

        if(lambda_0_l > 0.0)
            q_0 = &q_l;
        else
        {
            q_m = centered_rarefaction(q_l, u_max, gamma, inv_gamma);
            q_0 = &q_m;
        }
    }
    else if(std::abs(q_l.u() - q_r.u()) < epsilon())
    {   // case 0
        speeds[0] = 0.0;
        waves [0] = q(0.0, 0.0);

        speeds[1] = q_r.u();
        waves [1] = q_r - q_l;

        q_0 = &q_l;
    }
    else if(q_l.u() > q_r.u())
    {   // case 1
        q_m = rho_middle(q_l, q_r, inv_u_max, inv_gamma);

        const T flux_0_diff = q_m.flux_0() - q_l.flux_0();
        speeds[0] = std::abs(flux_0_diff) < epsilon() ? 0.0 : flux_0_diff/(q_m.rho() - q_l.rho());
        waves [0] = q_m - q_l;

        speeds[1] = q_r.u();
        waves [1] = q_r - q_m;

        q_0 = (speeds[0] >= 0.0) ? &q_l : &q_m;
    }
    else if(u_max + q_l.u() - q_l.u_eq() > q_r.u())
    {   // case 2
        q_m = rho_middle(q_l, q_r, inv_u_max, inv_gamma);

        const T lambda0_l = q_l.lambda_0(u_max, gamma);
        const T lambda0_m = q_m.lambda_0(u_max, gamma);

        speeds[0] = (lambda0_l + lambda0_m)/2;
        waves [0] = q_m - q_l;

        speeds[1] = q_r.u();
        waves [1] = q_r - q_m;

        if(lambda0_l >= 0.0)
            q_0 = &q_l;
        else if(lambda0_m < 0.0)
            q_0 = &q_m;
        else
        {
            q_m = centered_rarefaction(q_l, u_max, gamma, inv_gamma);
            q_0 = &q_m;
        }
    }
    else
    {   // case 3
        q_m.rho()  = 0.0;
        q_m.y()    = 0.0;
        q_m.u_eq() = u_max;
        q_m.u()    = u_max + q_l.u() - q_l.u_eq();

        const T lambda0_l = q_l.lambda_0(u_max, gamma);
        const T lambda0_m = q_m.u();

        speeds[0] = (lambda0_l + lambda0_m)/2;
        waves [0] = q_m - q_l;

        speeds[1] = q_r.u();
        waves [1] = q_r - q_m;

        if(lambda0_l >= 0.0)
            q_0 = &q_l;
        else
        {
            q_m = centered_rarefaction(q_l, u_max, gamma, inv_gamma);
            q_0 = &q_m;
        }
    }

    left_fluctuation  = q_0->flux() - q_l. flux();
    right_fluctuation = q_r. flux() - q_0->flux();
}

template <typename T>
inline void arz<T>::riemann_solution::starvation_riemann(const full_q &__restrict__ q_r,
                                                         const T                    u_max,
                                                         const T                    inv_u_max,
                                                         const T                    gamma,
                                                         const T                    inv_gamma)
{
    if(q_r.rho() < VACUUM_EPS)
    {
        clear();
        return;
    }

    speeds[0]         = 0.0;
    waves [0]         = q(0.0, 0.0);
    speeds[1]         = q_r.u();
    waves [1]         = q_r;
    left_fluctuation  = q(0.0, 0.0);
    right_fluctuation = q_r.flux();
}

template <typename T>
inline void arz<T>::riemann_solution::stop_riemann(const full_q &__restrict__ q_l,
                                                   const T                    u_max,
                                                   const T                    inv_u_max,
                                                   const T                    gamma,
                                                   const T                    inv_gamma)
{
    full_q q_m;
    q_m.u_eq() = q_l.u_eq() - q_l.u();
    q_m.u()    = 0.0;
    q_m.rho()  = eq::inv_u_eq(q_m.u_eq(), inv_u_max, inv_gamma);
    q_m.y()    = -q_m.rho()*q_m.u_eq();

    const T rho_diff  = q_m.rho() - q_l.rho();
    speeds[0]         = rho_diff < VACUUM_EPS ? 0.0 : -q_l.flux_0()/rho_diff;
    waves [0]         = -q_l;
    speeds[1]         = 0.0;
    waves [1]         = q(0.0, 0.0);
    left_fluctuation  = -q_l.flux();
    right_fluctuation = q(0.0, 0.0);
}

template <typename T>
inline void arz<T>::riemann_solution::clear()
{
    memset(this, 0, sizeof(*this));
}

template <typename T>
inline bool arz<T>::riemann_solution::check() const
{
    return std::isfinite(waves[0][0])
    && std::isfinite(waves[0][1])
    && std::isfinite(waves[1][0])
    && std::isfinite(waves[1][1])
    && std::isfinite(left_fluctuation[0])
    && std::isfinite(left_fluctuation[1])
    && std::isfinite(right_fluctuation[0])
    && std::isfinite(right_fluctuation[1])
    && std::isfinite(speeds[0]) && std::isfinite(speeds[1]);
}

#endif