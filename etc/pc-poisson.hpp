#ifndef _PC_POISSON_HPP_
#define _PC_POISSON_HPP_

#include "pc-integrate.hpp"

template <typename T>
inline T exp_rvar(const T r)
{
    return -std::log(r);
}

template <typename T>
struct inhomogeneous_poisson
{
    inhomogeneous_poisson(const T in_start, const pc_data<T> &in_pc) : t(in_start),
                                                                       pc(in_pc),
                                                                       arg(pc.integrate(t))

    {
    }

    T next()
    {
        const T u = drand48();
        const T e = exp_rvar(u);
        arg += e;
        t = pc.inv_integrate(arg);
        return t;
    }

    T                 t;
    const pc_data<T> &pc;
    T                 arg;
};

#endif
