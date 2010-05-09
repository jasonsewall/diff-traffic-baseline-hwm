#include <Magick++.h>
#include <GL/glew.h>
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/gl.h>
#include <FL/glu.h>
#include <FL/glut.h>
#include "libroad/hwm_network.hpp"
#include "libroad/geometric.hpp"
#include "libhybrid/hybrid-sim.hpp"
#include "libroad/hwm_draw.hpp"
#include "libhybrid/timer.hpp"

static inline void blackbody(float *rgb, const float val)
{
   if(val <= 0.0) // clamp low to black
       rgb[0] = rgb[1] = rgb[2] = 0.0f;
   else if(val >= 1.0f) // and high to white
       rgb[0] = rgb[1] = rgb[2] = 1.0f;
   else if(val < 1.0f/3.0f) // go to [1, 0, 0] over [0, 1/3)
   {
       rgb[0] = val*3.0f;
       rgb[1] = 0.0f;
       rgb[2] = 0.0f;
   }
   else if(val < 2.0f/3.0f)  // go to [1, 1, 0] over [1/3, 2/3)
   {
       rgb[0] = 1.0f;
       rgb[1] = (val-1.0f/3.0f)*3.0f;
       rgb[2] = 0.0f;
   }
   else // go to [1, 1, 1] over [2/3, 1.0)
   {
       rgb[0] = 1.0f;
       rgb[1] = 1.0f;
       rgb[2] = (val-2.0f/3.0f)*3.0f;
   }
   return;
}

typedef enum { LEFT, CENTER_X, RIGHT }  text_alignment_x;
typedef enum { TOP, CENTER_Y, BOTTOM }  text_alignment_y;

static void put_text(cairo_t * cr, const std::string &str, float x, float y, const text_alignment_x align_x, const text_alignment_y align_y)
{
    cairo_save (cr);

    cairo_font_extents_t fe;
    cairo_text_extents_t te;

    cairo_font_extents (cr, &fe);
    cairo_text_extents (cr, str.c_str(), &te);

    switch(align_x)
    {
    case LEFT:
        x += -te.x_bearing;
        break;
    case CENTER_X:
        x += -te.width/2-te.x_bearing;
        break;
    case RIGHT:
        x += - (te.x_bearing + te.width);
        break;
    default:
        assert(0);
    };

    switch(align_y)
    {
    case TOP:
        y +=  - te.y_bearing;
        break;
    case CENTER_Y:
        y += te.height/2;
        break;
    case BOTTOM:
        y +=  0.0;
        break;
    default:
        assert(0);
    };

    cairo_move_to(cr, x, y);

    cairo_show_text(cr, str.c_str());

    cairo_restore(cr);
}

struct tex_car_draw
{
    tex_car_draw() : car_tex(0)
    {
    }

    bool initialized() const
    {
        return glIsTexture(car_tex);
    }

    void initialize(const float        car_width_,
                    const float        car_length_,
                    const float        car_height_,
                    const float        car_rear_axle_,
                    const std::string &str)
    {
        car_width     = car_width_;
        car_length    = car_length_;
        car_height    = car_height_;
        car_rear_axle = car_rear_axle_;

        glGenTextures(1, &car_tex);
        glBindTexture (GL_TEXTURE_2D, car_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        Magick::Image im(str);
        const vec2i dim(im.columns(), im.rows());
        unsigned char *pix = new unsigned char[dim[0]*dim[1]*4];
        im.write(0, 0, dim[0], dim[1], "RGBA", Magick::CharPixel, pix);
        gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, dim[0], dim[1],
                          GL_RGBA, GL_UNSIGNED_BYTE, pix);
        delete[] pix;
    }

    void draw() const
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture (GL_TEXTURE_2D, car_tex);
        glPushMatrix();
        glTranslatef(-car_length+car_rear_axle, 0, 0);
        glScalef(car_length, car_width/2, 1);
        glBegin(GL_QUADS);
        glTexCoord2i(0,0);
        glVertex2i(0,-1);
        glTexCoord2i(1,0);
        glVertex2i(1,-1);
        glTexCoord2i(1,1);
        glVertex2i(1,1);
        glTexCoord2i(0,1);
        glVertex2i(0,1);
        glEnd();

        glPopMatrix();
    }

    float  car_width;
    float  car_length;
    float  car_height;
    float  car_rear_axle;
    GLuint car_tex;
};

static const float CAR_LENGTH    = 4.5f;
//* This is the position of the car's axle from the FRONT bumper of the car
static const float CAR_REAR_AXLE = 3.5f;

class fltkview : public Fl_Gl_Window
{
public:
    fltkview(int x, int y, int w, int h, const char *l) : Fl_Gl_Window(x, y, w, h, l),
                                                          lastpick(0),
                                                          net(0),
                                                          netaux(0),
                                                          glew_state(GLEW_OK+1),
                                                          road_tex_(0),
                                                          background_tex_(0),
                                                          back_image(0),
                                                          back_image_center(0),
                                                          back_image_scale(1),
                                                          back_image_yscale(1),
                                                          overlay_tex_(0),
                                                          continuum_tex_(0),
                                                          drawing(false),
                                                          light_position(50.0, 100.0, 50.0, 1.0),
                                                          sim(0),
                                                          t(0)
    {
        this->resizable(this);
        frame_timer.reset();
        frame_timer.start();
    }

    void setup_light()
    {
        static const GLfloat amb_light_rgba[]  = { 0.1, 0.1, 0.1, 1.0 };
        static const GLfloat diff_light_rgba[] = { 0.7, 0.7, 0.7, 1.0 };
        static const GLfloat spec_light_rgba[] = { 1.0, 1.0, 1.0, 1.0 };
        static const GLfloat spec_material[]   = { 1.0, 1.0, 1.0, 1.0 };
        static const GLfloat material[]        = { 1.0, 1.0, 1.0, 1.0 };
        static const GLfloat shininess         = 100.0;

        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glPushMatrix();
        glLoadIdentity();
        glLightfv(GL_LIGHT0, GL_POSITION, light_position.data());
        glPopMatrix();
        glLightfv(GL_LIGHT0, GL_AMBIENT, amb_light_rgba );
        glLightfv(GL_LIGHT0, GL_DIFFUSE, diff_light_rgba );
        glLightfv(GL_LIGHT0, GL_SPECULAR, spec_light_rgba );
        glMaterialfv( GL_FRONT, GL_AMBIENT, material );
        glMaterialfv( GL_FRONT, GL_DIFFUSE, material );
        glMaterialfv( GL_FRONT, GL_SPECULAR, spec_material );
        glMaterialfv( GL_FRONT, GL_SHININESS, &shininess);
    }

    void init_glew()
    {
        glew_state = glewInit();
        if (GLEW_OK != glew_state)
        {
            /* Problem: glewInit failed, something is seriously wrong. */
            std::cerr << "Error: " << glewGetErrorString(glew_state)  << std::endl;
        }
        std::cerr << "Status: Using GLEW " << glewGetString(GLEW_VERSION) << std::endl;
    }

    void init_textures()
    {
        if(!glIsTexture(road_tex_))
        {
            glGenTextures(1, &road_tex_);
            glBindTexture (GL_TEXTURE_2D, road_tex_);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            retex_roads(center, scale, vec2i(w(), h()));
        }
        if(back_image && !glIsTexture(background_tex_))
        {
            glGenTextures(1, &background_tex_);
            glBindTexture (GL_TEXTURE_2D, background_tex_);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

            Magick::Image im(*back_image);
            im.flip();
            back_image_dim = vec2i(im.columns(), im.rows());
            unsigned char *pix = new unsigned char[back_image_dim[0]*back_image_dim[1]*4];
            im.write(0, 0, back_image_dim[0], back_image_dim[1], "RGBA", Magick::CharPixel, pix);
            glTexImage2D (GL_TEXTURE_2D,
                          0,
                          GL_RGBA,
                          back_image_dim[0],
                          back_image_dim[1],
                          0,
                          GL_RGBA,
                          GL_UNSIGNED_BYTE,
                          pix);
            delete[] pix;
        }
        if(!glIsTexture(overlay_tex_))
        {
            glGenTextures(1, &overlay_tex_);
            glBindTexture (GL_TEXTURE_2D, overlay_tex_);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            retex_overlay(center, scale, vec2i(w(), h()));
        }
        if(!glIsTexture(continuum_tex_))
        {
            glGenTextures(1, &continuum_tex_);
            glBindTexture (GL_TEXTURE_2D, continuum_tex_);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        }
    }

    void retex_roads(const vec2f &my_center, const float my_scale, const vec2i &im_res)
    {
        cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                         im_res[0],
                                                         im_res[1]);
        cairo_t         *cr = cairo_create(cs);
        cairo_set_source_rgba(cr, 0.0, 0, 0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);

        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

        cairo_translate(cr,
                        im_res[0]/2,
                        im_res[1]/2);

        cairo_scale(cr,
                    im_res[0]/my_scale,
                    im_res[1]/my_scale);


        if(im_res[0] > im_res[1])
            cairo_scale(cr,
                        static_cast<float>(im_res[1])/im_res[0],
                        1.0);
        else
            cairo_scale(cr,
                        1.0,
                        static_cast<float>(im_res[0])/im_res[1]);

        cairo_translate(cr,
                        -my_center[0],
                        -my_center[1]);

        cairo_set_line_width(cr, 0.5);
        netaux->cairo_roads(cr);

        cairo_destroy(cr);

        glBindTexture (GL_TEXTURE_2D, road_tex_);
        glTexImage2D (GL_TEXTURE_2D,
                      0,
                      GL_RGBA,
                      im_res[0],
                      im_res[1],
                      0,
                      GL_BGRA,
                      GL_UNSIGNED_BYTE,
                      cairo_image_surface_get_data(cs));

        cairo_surface_destroy(cs);

        cscale_to_box(tex_low, tex_high, my_center, my_scale, im_res);
    }

    void retex_overlay(const vec2f &my_center, const float my_scale, const vec2i &im_res)
    {
        cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                         im_res[0],
                                                         im_res[1]);
        cairo_t         *cr = cairo_create(cs);
        cairo_set_source_rgba(cr, 0.0, 0, 0, 0.0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);

        cairo_set_font_size (cr, 20);
        cairo_select_font_face (cr, "SANS",
                                CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_source_rgba (cr, 1.0f, 1.0f, 1.0f, 1.0f);

        cairo_translate(cr,
                        im_res[0]/2,
                        im_res[1]/2);
        cairo_scale(cr, 1, -1);
        cairo_translate(cr,
                        -im_res[0]/2,
                        -im_res[1]/2);
        put_text(cr, boost::str(boost::format("real time:     %8.3fs") % t), 10, 5, LEFT, TOP);
        put_text(cr, boost::str(boost::format("stored range: (%8.3fs, %8.3fs)") % hci->times[0] % hci->times[1]), 10, 30, LEFT, TOP);

        cairo_identity_matrix(cr);

        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

        cairo_translate(cr,
                        im_res[0]/2,
                        im_res[1]/2);

        cairo_scale(cr,
                    im_res[0]/my_scale,
                    im_res[1]/my_scale);


        if(im_res[0] > im_res[1])
            cairo_scale(cr,
                        static_cast<float>(im_res[1])/im_res[0],
                        1.0);
        else
            cairo_scale(cr,
                        1.0,
                        static_cast<float>(im_res[0])/im_res[1]);

        cairo_translate(cr,
                        -my_center[0],
                        -my_center[1]);
        cairo_matrix_t cmat;
        cairo_get_matrix(cr, &cmat);

        BOOST_FOREACH(const aabb2d &r, rectangles)
        {
            cairo_set_matrix(cr, &cmat);
            cairo_rectangle(cr, r.bounds[0][0], r.bounds[0][1], r.bounds[1][0]-r.bounds[0][0], r.bounds[1][1]-r.bounds[0][1]);
            cairo_set_source_rgba(cr, 67/255.0, 127/255.0, 195/255.0, 0.5);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 17/255.0, 129/255.0, 255/255.0, 0.7);
            cairo_identity_matrix(cr);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
        }

        if(drawing)
        {
            const vec2f low(std::min(first_point[0], second_point[0]),
                            std::min(first_point[1], second_point[1]));
            const vec2f high(std::max(first_point[0], second_point[0]),
                             std::max(first_point[1], second_point[1]));

            cairo_set_matrix(cr, &cmat);
            cairo_rectangle(cr, low[0], low[1], high[0]-low[0], high[1]-low[1]);
            cairo_set_source_rgba(cr, 67/255.0, 127/255.0, 195/255.0, 0.5);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 17/255.0, 129/255.0, 255/255.0, 0.7);
            cairo_identity_matrix(cr);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
        }

        cairo_destroy(cr);

        glBindTexture(GL_TEXTURE_2D, overlay_tex_);
        glTexImage2D (GL_TEXTURE_2D,
                      0,
                      GL_RGBA,
                      im_res[0],
                      im_res[1],
                      0,
                      GL_BGRA,
                      GL_UNSIGNED_BYTE,
                      cairo_image_surface_get_data(cs));

        cairo_surface_destroy(cs);
    }

    void draw()
    {
        frame_timer.stop();
        t += frame_timer.interval_S();
        frame_timer.reset();
        frame_timer.start();

        if(sim && hci)
        {
            while(t > hci->times[1])
            {
                hci->capture(*sim);
                sim->hybrid_step();
            }
        }

        if (!valid())
        {
            glViewport(0, 0, w(), h());
            glClearColor(0.0, 0.0, 0.0, 0.0);

            if(GLEW_OK != glew_state)
                init_glew();
            if(!car_drawer.initialized())
                car_drawer.initialize(0.8*sim->hnet->lane_width,
                                      CAR_LENGTH,
                                      1.5f,
                                      CAR_REAR_AXLE,
                                      "car-top.png");

            if(!network_drawer.initialized())
                network_drawer.initialize(sim->hnet, 0.05f);

            init_textures();
            setup_light();
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();

        vec2f lo, hi;
        cscale_to_box(lo, hi, center, scale, vec2i(w(), h()));
        gluOrtho2D(lo[0], hi[0], lo[1], hi[1]);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glColor3f(1.0, 1.0, 1.0);
        if(back_image)
        {
            glBindTexture (GL_TEXTURE_2D, background_tex_);

            glPushMatrix();
            glTranslatef(-back_image_center[0], -back_image_center[1], 0);
            glScalef    (back_image_scale, back_image_yscale*back_image_scale,   1);

            glTranslatef(-back_image_dim[0]/2, -back_image_dim[1]/2, 0);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0);
            glVertex2i(0, 0);
            glTexCoord2f(1.0, 0.0);
            glVertex2i(back_image_dim[0], 0);
            glTexCoord2f(1.0, 1.0);
            glVertex2iv(back_image_dim.data());
            glTexCoord2f(0.0, 1.0);
            glVertex2i(0, back_image_dim[1]);
            glEnd();
            glPopMatrix();
        }

        glBindTexture (GL_TEXTURE_2D, road_tex_);

        glPushMatrix();
        glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0);
        glVertex2fv(tex_low.data());
        glTexCoord2f(1.0, 0.0);
        glVertex2f(tex_high[0], tex_low[1]);
        glTexCoord2f(1.0, 1.0);
        glVertex2fv(tex_high.data());
        glTexCoord2f(0.0, 1.0);
        glVertex2f(tex_low[0], tex_high[1]);
        glEnd();
        glPopMatrix();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_TEXTURE_2D);
        glBindTexture (GL_TEXTURE_2D, continuum_tex_);
        std::vector<vec4f> colors;
        BOOST_FOREACH(hybrid::lane &l, sim->lanes)
        {
            if(!l.parent->active || !l.is_macro())
                continue;

            colors.resize(l.N);
            for(size_t i = 0; i < l.N; ++i)
            {
                float val;
                //                if(drawfield == RHO)
                val = l.q[i].rho();
                // else
                //     val = arz<float>::eq::u(l.q[i].rho(),
                //                             l.q[i].y(),
                //                             l.parent->speedlimit,
                //                             sim->gamma)/l.parent->speedlimit;

                blackbody(colors[i].data(), val);
                colors[i][3] = 1.0f;
            }

            glTexImage2D (GL_TEXTURE_2D,
                          0,
                          GL_RGBA,
                          l.N,
                          1,
                          0,
                          GL_RGBA,
                          GL_FLOAT,
                          colors[0].data());

            network_drawer.draw_lane_solid(l.parent->id);
        }

        if(hci)
        {
            glColor3f(1.0, 0.0, 0.0);
            BOOST_FOREACH(hybrid::car_interp::car_hash::value_type &cs, hci->car_data[0])
            {
                if(!hci->in_second(cs.first))
                    continue;

                mat4x4f trans(hci->point_frame(cs.first, t));
                mat4x4f ttrans(tvmet::trans(trans));
                glPushMatrix();
                glMultMatrixf(ttrans.data());
                car_drawer.draw();
                glPopMatrix();
            }
        }

        glColor3f(1.0, 1.0, 1.0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture (GL_TEXTURE_2D, overlay_tex_);
        retex_overlay(center, scale, vec2i(w(), h()));
        glPushMatrix();
        glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0);
        glVertex2fv(lo.data());
        glTexCoord2f(1.0, 0.0);
        glVertex2f(hi[0], lo[1]);
        glTexCoord2f(1.0, 1.0);
        glVertex2fv(hi.data());
        glTexCoord2f(0.0, 1.0);
        glVertex2f(lo[0], hi[1]);
        glEnd();
        glPopMatrix();

        glDisable(GL_TEXTURE_2D);

        glFlush();
        glFinish();
    }

    int handle(int event)
    {
        switch(event)
        {
        case FL_PUSH:
            {
                const vec2i xy(Fl::event_x(),
                               Fl::event_y());
                const vec2f world(world_point(vec2i(xy[0], h()-xy[1]), center, scale, vec2i(w(), h())));

                if(Fl::event_button() == FL_LEFT_MOUSE)
                    first_point = world;

                lastpick = world;
            }
            take_focus();
            return 1;
        case FL_RELEASE:
            {
                if(Fl::event_button() == FL_LEFT_MOUSE)
                {
                    if(drawing)
                    {
                        rectangles.clear();
                        rectangles.push_back(aabb2d());
                        rectangles.back().enclose_point(first_point[0], first_point[1]);
                        rectangles.back().enclose_point(second_point[0], second_point[1]);
                        drawing = false;
                        query_results = netaux->road_space.query(rectangles.back());

                        BOOST_FOREACH(hybrid::lane &l, sim->lanes)
                        {
                            l.updated_flag = false;
                        }
                        BOOST_FOREACH(hwm::network_aux::road_spatial::entry &e, query_results)
                        {
                            BOOST_FOREACH(hwm::network_aux::road_rev_map::lane_cont::value_type &lcv, *e.lc)
                            {
                                hwm::lane    &hwm_l = *(lcv.second.lane);
                                hybrid::lane &hyb_l = *(hwm_l.user_data<hybrid::lane>());
                                if(!hyb_l.updated_flag)
                                {
                                    hyb_l.convert_to_micro(*sim);
                                    hyb_l.updated_flag = true;
                                }
                            }
                        }
                        BOOST_FOREACH(hybrid::lane &l, sim->lanes)
                        {
                            if(!l.updated_flag)
                            {
                                l.convert_to_macro(*sim);
                                l.updated_flag = true;
                            }
                        }
                    }
                }
            }
            return 1;
        case FL_DRAG:
            {
                const vec2i xy(Fl::event_x(),
                               Fl::event_y());
                const vec2f world(world_point(vec2i(xy[0], h()-xy[1]), center, scale, vec2i(w(), h())));
                vec2f dvec(0);
                if(Fl::event_button() == FL_LEFT_MOUSE)
                {
                    drawing      = true;
                    second_point = world;
                }
                else if(Fl::event_button() == FL_MIDDLE_MOUSE)
                {
                    dvec = vec2f(world - lastpick);
                    center -= dvec;
                    retex_roads(center, scale, vec2i(w(), h()));
                }
                else if(Fl::event_button() == FL_RIGHT_MOUSE)
                {
                    dvec = vec2f(world - lastpick);
                    back_image_center -= dvec;
                }
                lastpick = world-dvec;
            }
            take_focus();
            return 1;
        case FL_KEYBOARD:
            switch(Fl::event_key())
            {
            case ' ':
                break;
            }
            return 1;
        case FL_MOUSEWHEEL:
            {
                const vec2i xy(Fl::event_x(),
                               Fl::event_y());
                const vec2i dxy(Fl::event_dx(),
                                Fl::event_dy());
                const float fy = copysign(0.5, dxy[1]);

                if(Fl::event_state() & FL_SHIFT)
                {
                    if(Fl::event_state() & FL_CTRL)
                        back_image_yscale *= std::pow(2.0, 0.1*fy);
                    else
                        back_image_scale  *= std::pow(2.0, 0.5*fy);
                }
                else
                {
                    scale *= std::pow(2.0, fy);
                    retex_roads(center, scale, vec2i(w(), h()));
                }
            }
            take_focus();
            return 1;
        default:
            // pass other events to the base class...
            return Fl_Gl_Window::handle(event);
        }
    }

    vec2f lastpick;

    hwm::network     *net;
    hwm::network_aux *netaux;
    vec2f             tex_low;
    vec2f             tex_high;
    vec2f             center;
    float             scale;

    GLuint   glew_state;
    GLuint   road_tex_;
    GLuint   background_tex_;
    char   **back_image;
    vec2i    back_image_dim;
    vec2f    back_image_center;
    float    back_image_scale;
    float    back_image_yscale;

    GLuint   overlay_tex_;
    GLuint   continuum_tex_;

    std::vector<aabb2d>                                rectangles;
    bool                                               drawing;
    vec2f                                              first_point;
    vec2f                                              second_point;
    std::vector<hwm::network_aux::road_spatial::entry> query_results;

    tex_car_draw       car_drawer;
    hwm::network_draw  network_drawer;

    vec4f               light_position;
    hybrid::simulator  *sim;
    hybrid::car_interp *hci;
    float               t;
    timer               frame_timer;
};

void draw_callback(void *v)
{
    reinterpret_cast<fltkview*>(v)->redraw();
    Fl::repeat_timeout(1.0/30.0, draw_callback, v);
}

int main(int argc, char *argv[])
{
    std::cout << libroad_package_string() << std::endl;
    if(argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input network> [background image]" << std::endl;
        return 1;
    }
    hwm::network net(hwm::load_xml_network(argv[1], vec3f(1.0, 1.0, 1.0f)));

    net.build_intersections();
    net.build_fictitious_lanes();
    net.auto_scale_memberships();
    net.center();
    std::cerr << "HWM net loaded successfully" << std::endl;

    try
    {
        net.check();
        std::cerr << "HWM net checks out" << std::endl;
    }
    catch(std::runtime_error &e)
    {
        std::cerr << "HWM net doesn't check out: " << e.what() << std::endl;
        exit(1);
    }

    hwm::network_aux neta(net);

    hybrid::simulator s(&net,
                        4.5,
                        1.0);
    s.micro_initialize(0.73,
                       1.67,
                       33,
                       4);
    s.macro_initialize(0.5, 2.1*4.5, 0.0f);

    BOOST_FOREACH(hybrid::lane &l, s.lanes)
    {
        l.current_cars().clear();
        l.sim_type = hybrid::MICRO;

        const int cars_per_lane = 2;
        double    p             = -s.rear_bumper_offset()*l.inv_length;

        for (int i = 0; i < cars_per_lane; i++)
        {
            //TODO Just creating some cars here...
            l.current_cars().push_back(s.make_car(p, 0, 0));

            //Cars need a minimal distance spacing
            p += (30.0 * l.inv_length);
            if(p + s.front_bumper_offset()*l.inv_length >= 1.0)
                break;
        }
    }

    //    s.settle(0.033);

    hybrid::car_interp hci(s);
    s.hybrid_step();
    hci.capture(s);
    s.hybrid_step();

    fltkview mv(0, 0, 500, 500, "fltk View");
    mv.net    = &net;
    mv.netaux = &neta;
    mv.sim    = &s;
    mv.hci    = &hci;
    mv.t      = hci.times[0];

    if(argc == 3)
        mv.back_image = argv+2;

    Fl::add_timeout(1.0/30.0, draw_callback, &mv);

    vec3f low(FLT_MAX);
    vec3f high(-FLT_MAX);
    net.bounding_box(low, high);
    box_to_cscale(mv.center, mv.scale, sub<0,2>::vector(low), sub<0,2>::vector(high), vec2i(500,500));

    mv.take_focus();
    Fl::visual(FL_DOUBLE|FL_DEPTH);

    mv.show(1, argv);
    return Fl::run();
}