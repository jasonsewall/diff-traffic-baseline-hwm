#include "hybrid-sim.hpp"

#include <iostream>

namespace hybrid
{
    float car::check_lane(const lane* l, const float param, const double timestep, const simulator &sim)
    {
        float polite = 1;
        car potential_follower(0, 0, 0, 0);
        car potential_leader(0, 0, 0, 0);
        bool found_a_leader = false;
        bool found_a_follower = false;


        //tmp should do a binary search for positon
        for (int i = 0; i < (int)l->cars[0].size(); i++)
        {

            potential_leader   = l->current_car(i);

            std::cout << "other " << potential_leader.position << " , curr " << param << std::endl;

            if (potential_leader.position > param)
            {
                std::cout << "found leader" << std::endl;
                found_a_leader = true;
                break;
            }

            potential_follower = l->current_car(i);


            if (potential_follower.position <= param)
            {
                std::cout << "found follower" << std::endl;
                found_a_follower = true;
            }
        }

        if (found_a_leader == false)
        {
            potential_follower = l->cars[0].back();
            float vel, pos;
            find_free_dist_and_vel(*l, vel, pos, sim);
            potential_leader = car(0, pos, vel, 0);
        }

        //Calc what the acceleration would be for this car were the lane change made.
        std::cout << potential_leader.velocity << " " << velocity << " " << std::abs(potential_leader.position - param) << std::endl;
        float a_hat = sim.acceleration(potential_leader.velocity, velocity, std::abs(potential_leader.position*l->length - param*l->length));

        //TODO need to search backward for follower if not found.

        //Calc what the deceleration would be for the new follower car.
        float f_hat = 0;
        if (found_a_follower)
            f_hat = sim.acceleration(velocity, potential_follower.velocity, std::abs(param*l->length - potential_follower.position*l->length));
        else
            potential_follower = car(0, 0, 0, 0);

        std::cout << "found follower " << found_a_follower << std::endl;
        std::cout << a_hat << " pot new accel " << std::endl;
        std::cout << acceleration << " cur accel " << std::endl;
        std::cout << potential_follower.acceleration << " is pot follower curr accel " << std::endl;
        std::cout << f_hat << " is potential accel for new follower" << std::endl;

        return (a_hat - acceleration) + polite*(f_hat - potential_follower.acceleration);
    }

    void car::check_if_valid_acceleration(lane& l, double timestep)
    {
        double tmp_position = position + (velocity * timestep) * l.inv_length;

        lane* curr = &l;
        lane* destination_lane = &l;

        while(tmp_position >= 1.0)
        {
            hwm::lane *hwm_downstream = curr->parent->downstream_lane();

            if (!hwm_downstream)
            {
                std::cout << "error" << std::endl;
                assert(0);
            }

            if (!hwm_downstream->active)
            {
                std::cout << "error" << std::endl;
                assert(0);
            }

            lane* downstream = hwm_downstream->user_data<lane>();

            tmp_position = (tmp_position - 1.0f) * curr->length * downstream->inv_length;

            destination_lane = downstream;

            if (tmp_position >= 1.0)
            {
                curr = downstream;
            }
        }
    }

    void car::compute_acceleration(const car &next, const float distance, const simulator &sim)
    {
        acceleration = sim.acceleration(next.velocity, velocity, distance);
    }

    void car::find_free_dist_and_vel(const lane &l, float& next_velocity, float& distance, const simulator& sim)
    {
        const double min_for_free_movement = 1000;

        hwm::lane *hwm_downstream = l.parent->downstream_lane();

        next_velocity = 0.0f;
        distance      = (1.0 - position) * l.length - sim.rear_bumper_offset();
        if(hwm_downstream)
            hwm_downstream->user_data<lane>()->distance_to_car(distance, next_velocity, min_for_free_movement, sim);
    }

    void car::compute_intersection_acceleration(const simulator &sim, const lane &l)
    {
        float next_velocity;
        float distance;
        find_free_dist_and_vel(l, next_velocity, distance, sim);

        acceleration = sim.acceleration(next_velocity, velocity, distance);
    }

    void car::integrate(const double timestep, const lane& l)
    {
        position += (velocity * timestep) * l.inv_length;
        velocity = std::max(0.0, velocity + acceleration * timestep);
    }

    void lane::micro_distance_to_car(float &distance, float &velocity, const float distance_max, const simulator &sim) const
    {
        if(current_cars().empty())
        {
            distance += length;
            if(distance >= distance_max)
            {
                velocity = 0.0f;
                distance = distance_max;
            }
            else
            {
                hwm::lane *hwm_downstream = parent->downstream_lane();
                if(hwm_downstream)
                    hwm_downstream->user_data<lane>()->distance_to_car(distance, velocity, distance_max, sim);
                else
                    velocity = 0.0f;
            }
        }
        else
        {
            velocity  = current_cars().front().velocity;
            distance += current_cars().front().position*length;
        }

        return;
    }

    void lane::compute_merges(const double timestep, const simulator& sim)
    {
        float threshold = 0.5;
        for(int i = 0; i < ((int)current_cars().size()); ++i)
        {
            //Check if car decides to merge
            float right_param = current_car(i).position;
            std::cout << "r pos " << right_param << std::endl;
            hwm::lane* potential_right = parent->right_adjacency(right_param);
            std::cout << "r param " << right_param << std::endl;
            double right_accel = 0;
            lane* right_lane = 0;
            if (potential_right)
            {
                std::cout << "checking right" << std::endl;
                right_lane = potential_right->user_data<lane>();
                right_accel = current_car(i).check_lane(right_lane, right_param, timestep, sim);
            }

            float left_param = current_car(i).position;
            std::cout << "l pos " << left_param << std::endl;
            hwm::lane* potential_left = parent->left_adjacency(left_param);
            std::cout << "l param " << left_param << std::endl;
            double left_accel = 0;
            lane* left_lane = 0;
            if (potential_left)
            {
                std::cout << "checking left" << std::endl;
                left_lane = potential_left->user_data<lane>();
                left_accel = current_car(i).check_lane(left_lane, left_param, timestep, sim);
            }

            if (right_accel > threshold or left_accel > threshold)
            {
                if (right_accel > left_accel)
                {
                    std::cout << "Merge right" << std::endl;
                    current_car(i).position = right_param;
                    right_lane->next_cars().push_back(current_car(i));
                }
                else
                {
                    std::cout << "Merge left" << std::endl;
                    current_car(i).position = left_param;
                    left_lane->next_cars().push_back(current_car(i));
                }
            }
            else
            {
                next_cars().push_back(current_car(i));
            }
        }
    }


    void lane::compute_lane_accelerations(const double timestep, const simulator &sim)
    {
         if(current_cars().empty())
             return;

         for(size_t i = 0; i < current_cars().size()-1; ++i)
         {
             current_car(i).compute_acceleration(current_car(i+1), (current_car(i+1).position - current_car(i).position)*length, sim);
             //            current_car(i).check_if_valid_acceleration(*this, timestep);  For debugging only
         }


         current_cars().back().compute_intersection_acceleration(sim, *this);
         // current_cars().back().check_if_valid_acceleration(*this, timestep); For debugging only
     }

     double lane::settle_pass(const double timestep, const double epsilon, const double epsilon_2,
                              const simulator &sim)
     {
         double max_acceleration = epsilon;
         int i = static_cast<int>(current_cars().size())-1;
         while(!current_cars().empty() && i >= 0)
         {
             car &c = current_car(i);

             double last_acceleration = std::numeric_limits<double>::max();
             while(true)
             {
                 compute_lane_accelerations(timestep, sim);

                 c.velocity = std::max(c.velocity + c.acceleration * timestep,
                                        0.0);

                 max_acceleration = std::max(std::abs(c.acceleration), max_acceleration);

                 if( std::abs(c.acceleration) > std::abs(last_acceleration)
                     or c.position+sim.front_bumper_offset()*inv_length >= 1.0
                     or ((std::abs(c.acceleration - last_acceleration) < epsilon_2) and (std::abs(c.acceleration) > epsilon)))
                 {
                     for(int j = i; j < static_cast<int>(current_cars().size())-1; ++j)
                         current_car(j) = current_car(j+1);
                     current_cars().pop_back();
                     break;
                 }
                 else if(std::abs(c.acceleration) < epsilon)
                 {
                     --i;
                     break;
                 }

                 last_acceleration = c.acceleration;
             }
         }
         return max_acceleration;
     }

     void simulator::micro_initialize(const double in_a_max, const double in_a_pref, const double in_v_pref,
                                      const double in_delta)
     {
         a_max                 = in_a_max;
         a_pref                = in_a_pref;
         v_pref                = in_v_pref;
         delta                 = in_delta;
     }

     void simulator::settle(const double timestep)
     {
         static const double EPSILON   = 1;
         static const double EPSILON_2 = 0.01;

         double max_acceleration;
         do
         {
             max_acceleration = EPSILON;
             BOOST_FOREACH(lane &l, lanes)
             {
                 if(l.is_micro() && l.parent->active)
                     max_acceleration = std::max(l.settle_pass(timestep, EPSILON, EPSILON_2, *this),
                                                 max_acceleration);
             }

             std::cout << "Max acceleration in settle: " << max_acceleration << std::endl;
         }
         while(max_acceleration > EPSILON);
     }

     double simulator::acceleration(const double leader_velocity, const double follower_velocity, double distance) const
     {
         static const double s1 = 2;
         static const double T  = 1.6;
         const float EPSILON = std::numeric_limits<float>::min();

         //if (f.vel < 0) f.vel = 0; //TODO Should this be taken into account?

         const double optimal_spacing = s1 + T*follower_velocity + (follower_velocity*(follower_velocity - leader_velocity))/2*(std::sqrt(a_max*a_pref));

         if (distance > car_length)
             distance -= car_length;
         if (distance == 0)
             distance = EPSILON;

         const double t               = a_max*(1 - std::pow((follower_velocity / v_pref), delta) - std::pow((optimal_spacing/(distance)), 2));

         //assert(l.dist - f.dist > 0); //A leader needs to lead.
         return t;
     }

     void simulator::compute_accelerations(const double timestep)
     {
         BOOST_FOREACH(lane &l, lanes)
         {
             if (l.is_micro() && l.parent->active)
                 l.compute_lane_accelerations(timestep, *this);
         }

         BOOST_FOREACH(lane& l, lanes)
         {
             if (l.is_micro() and l.parent->active)
                 std::cout << "lane" << std::endl;
                 l.compute_merges(timestep, *this);
         }

         BOOST_FOREACH(lane& l, lanes)
         {
             l.car_swap();
         }
     }

     void simulator::update(const double timestep)
     {
         compute_accelerations(timestep);

         BOOST_FOREACH(lane &l, lanes)
         {
             if(l.is_micro() && l.parent->active)
             {
                 BOOST_FOREACH(car &c, l.current_cars())
                 {
                     c.integrate(timestep, l);
                 }
             }
         }

         std::cout << lanes.size() << std::endl;
         BOOST_FOREACH(lane &l, lanes)
         {
             if(l.is_micro() && l.parent->active)
             {
                 BOOST_FOREACH(car &c, l.current_cars())
                 {
                     lane* destination_lane = &l;
                     lane* curr = &l;

                     while(c.position >= 1.0)
                     {
                         hwm::lane *hwm_downstream = curr->parent->downstream_lane();
                         assert(hwm_downstream);
                         assert(hwm_downstream->active);

                         lane* downstream = hwm_downstream->user_data<lane>();

                         switch(downstream->sim_type)
                         {
                         case MICRO:
                             c.position = (c.position - 1.0f) * curr->length * downstream->inv_length;
                             // downstream->next_cars().push_back(c);
                             destination_lane = downstream;
                             break;
                        case MACRO: //TODO update for correctness
                            downstream->q[0].rho() = std::min(1.0f, downstream->q[0].rho() + car_length/downstream->h);
                            downstream->q[0].y()   = std::min(0.0f, arz<float>::eq::y(downstream->q[0].rho(), c.velocity,
                                                                                      hwm_downstream->speedlimit,
                                                                                      gamma));
                            assert(downstream->q[0].check());
                            break;
                        }

                        if (c.position >= 1.0)
                        {
                            curr = downstream;
                        }
                    }

                    assert(c.position < 1.0);

                    destination_lane->next_cars().push_back(c);
                }
            }
        }
    }

    void simulator::micro_cleanup()
    {
    }
}
