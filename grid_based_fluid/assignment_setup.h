#ifndef ASSIGNMENT_SETUP_H
#define ASSIGNMENT_SETUP_H
#include <igl/readMESH.h>
#include <igl/readOBJ.h>
#include <igl/writeOBJ.h>
#include <igl/readOFF.h>
#include <igl/boundary_facets.h>
#include <igl/volume.h>
#include <visualization.h>

#include <Util.h>
#include <iostream>
#include <algorithm>
#include <pcg_solver.h>
#include <unordered_map>
#include <math.h>
#include <cmath>

double t = 0.;
int iter = 0;
// grid const info
const double l = 0.6;  // container length, width, height
const double h = 0.4;
const double w = 0.6;
const double dx = 0.1; // mac grid cell size
// handle double error
const int n_z = (int)ceil(2*w/dx);
const int n_x = (int)ceil(2*l/dx);
const int n_y = (int)ceil(4*h/dx);

// fluid initial
const Eigen::Vector3d fluid_pos = Eigen::Vector3d(0., 0.8, 0.);
const double fluid_rx = 0.3;
const double fluid_ry = 0.3;
const double fluid_rz = 0.3;

// sim params
const double g = -9.82;
double visc = 0.01;
double density = 100.;
bool simulation_pause = true;

// ptrs
Util::MacGrid *mac_grid_ptr;
Util::MarkerParticle *marker_particles_ptr;

/* Some control keys to interact with the fluid
 S: start/pause
 M: mesh/particle
 */
bool key_down_callback(igl::opengl::glfw::Viewer &viewer, unsigned char key, int modifiers) {
    
    if(key =='S') {
        simulation_pause = !simulation_pause;
    }
    if (key=='M'){
        // TODO
    }
    
    return false;
}


/*
 Add fluids (this is an example of an elipsoid into water)
 */
void add_fluid(Util::MacGrid mac_grid, std::vector<Eigen::Vector3i> &cells, std::vector<int> &indices){
    // drop fluid ball
    indices.clear();
    cells.clear();
    for (int i = 1; i < n_x-1; i++){
        for (int j = 1; j < n_y; j++){
            for (int k = 1; k < n_z-1; k++){
                Eigen::Vector3d min_vert = mac_grid.min_vert + Eigen::Vector3d(dx*(i+0.5), dx*(j+0.5), dx*(k+0.5));
                if (pow((min_vert[0]-fluid_pos[0])/fluid_rx, 2)+pow((min_vert[1]-fluid_pos[1])/fluid_ry, 2)+pow((min_vert[2]-fluid_pos[2])/fluid_rz, 2) < 1){
                    cells.push_back(Eigen::Vector3i(i, j, k));
                    indices.push_back((n_z*n_y)+i+n_z*j+k);
                }
                
                if (j == 1 || j == 2 || j==3){
                    cells.push_back(Eigen::Vector3i(i, j, k));
                    indices.push_back((n_z*n_y)+i+n_z*j+k);
                }
            }
        }
    }
}


/*
 initialize mac grid vel, pressure, label
 add fluid
 initialize marker particles pos
*/
inline void init_state(Util::MacGrid &mac_grid, Util::MarkerParticle &marker_particles){
    mac_grid_ptr = &mac_grid;
    marker_particles_ptr = &marker_particles;
    
    // grid setup
    mac_grid.init(l, w, h, dx);
    // marker particle setup
    std::vector<int> indices;
    std::vector<Eigen::Vector3i> cells;
    add_fluid(mac_grid, cells, indices);
    marker_particles.init(mac_grid, cells);
    
    // Defining the container
    Eigen::Vector3d m = mac_grid.min_vert + Eigen::Vector3d(dx, dx, dx);
    Eigen::Vector3d M = mac_grid.max_vert + Eigen::Vector3d(-dx, 0., -dx);
    
    // Corners of the container
    Eigen::MatrixXd V_box(8,3);
    V_box <<
    m(0), m(1), m(2),
    M(0), m(1), m(2),
    M(0), M(1), m(2),
    m(0), M(1), m(2),
    m(0), m(1), M(2),
    M(0), m(1), M(2),
    M(0), M(1), M(2),
    m(0), M(1), M(2);
    
    // Edges of the container
    Eigen::MatrixXi E_box(12,2);
    E_box <<
    0, 1,
    1, 2,
    2, 3,
    3, 0,
    4, 5,
    5, 6,
    6, 7,
    7, 4,
    0, 4,
    1, 5,
    2, 6,
    7 ,3;
    
    // Plot the the container
    for (unsigned i=0;i<E_box.rows(); ++i)
        Visualize::viewer().data().add_edges(V_box.row(E_box(i,0)),V_box.row(E_box(i,1)),Eigen::RowVector3d(1,0,0));

    // set up igl menu
    Visualize::viewer_menu().callback_draw_custom_window = [&]()
    {
        // Define next window position + size
        ImGui::SetNextWindowPos(ImVec2(0., 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(200, 160), ImGuiCond_FirstUseEver);
        ImGui::Begin(
                     "Customize Params Here", nullptr,
                     ImGuiWindowFlags_NoSavedSettings
                     );
        
        // add interactive var
        ImGui::PushItemWidth(-80);
        ImGui::DragScalar("Visc", ImGuiDataType_Double, &visc, 0.1, 0, 0, "%.4f");
        ImGui::PopItemWidth();
        
        ImGui::PushItemWidth(-80);
        ImGui::DragScalar("Density", ImGuiDataType_Double, &density, 0.1, 0, 0, "%.4f");
        ImGui::PopItemWidth();
        
        ImGui::End();
    };
    
    Visualize::viewer().callback_key_down = key_down_callback;
}


/*
 Get index into mac grid vel field with index i, j, k ================================
 */
int getIndexIntoVx(int i, int j, int k){
    return (n_y*n_z)*i+n_z*j+k;
}

int getIndexIntoVy(int i, int j, int k){
    return ((n_y+1)*n_z)*i+n_z*j+k;
    
}

int getIndexIntoVz(int i, int j, int k){
    return (n_y*(n_z+1))*i+(n_z+1)*j+k;
}


/*
 Interpolations================================================================
 */
double trilinear_vel(Eigen::VectorXd v, Eigen::Vector3d pos, int axis, bool debug=false){
    Eigen::Vector3d min_vert;
    int nx = n_x;
    int ny = n_y;
    int nz = n_z;
    if (axis == 0){
        min_vert = Eigen::Vector3d(-l, -h+0.5 * dx, -w+0.5*dx);
    }else if (axis == 1){
        min_vert = Eigen::Vector3d(-l+0.5*dx, -h, -w+0.5*dx);
        ny++;
    }else {
        min_vert = Eigen::Vector3d(-l+0.5*dx, -h+0.5*dx, -w);
        nz++;
    }
    
    int i = (int)floor((pos[0] - min_vert[0])/dx);
    int j = (int)floor((pos[1] - min_vert[1])/dx);
    int k = (int)floor((pos[2] - min_vert[2])/dx);
    double c000 = v[(ny * nz) * i + nz * j + k];
    double c100 = v[(ny * nz) * (i+1) + nz * j + k]; // x
    double c001 = v[(ny * nz) * i + nz * (j+1) + k]; // y
    double c101 = v[(ny * nz) * (i+1) + nz * (j+1) + k]; //xy
    double c010 = v[(ny * nz) * i + nz * j + k+1]; // z
    double c110 = v[(ny * nz) * (i+1) + nz * j + k+1]; // xz
    double c011 = v[(ny * nz) * i + nz * (j+1) + k+1]; // yz
    double c111 = v[(ny * nz) * (i+1) + nz * (j+1) + k+1]; // xyz
    Eigen::Vector3d min_corner = min_vert + Eigen::Vector3d(i * dx, j * dx, k * dx);
    
    double xd = (pos[0] - min_corner[0])/ dx;
    double yd = (pos[1] - min_corner[1])/ dx;
    double zd = (pos[2] - min_corner[2])/ dx;
    
    double c00 = c000 * (1 - xd) + c100 * xd;
    double c01 = c001 * (1 - xd) + c101 * xd;
    double c10 = c010 * (1 - xd) + c110 * xd;
    double c11 = c011 * (1 - xd) + c111 * xd;
    
    double c0 = c00 * (1-zd) + c10 * zd;
    double c1 = c01 * (1-zd) + c11 * zd;

    return c0 * (1-yd) + c1 * yd;
}

// TODO
void catmull_rom(){
    
}


/*
 Other functions =======================================================
 */
/* get the interpolated velocity at pos */
Eigen::Vector3d getVel(Util::MacGrid mac_grid, Eigen::Vector3d pos, bool debug=false){
    double x = trilinear_vel(mac_grid.vx, pos, 0, debug);
    double y = trilinear_vel(mac_grid.vy, pos, 1, debug);
    double z = trilinear_vel(mac_grid.vz, pos, 2, debug);
    return Eigen::Vector3d(x, y, z);
}

/* check if curr_p is inside the container */
bool inSolid(Eigen::Vector3d curr_p){
    return curr_p[0] < -l+dx || curr_p[0] > l-dx || curr_p[1] < -h+dx || curr_p[1] > 3*h || curr_p[2] < -w+dx || curr_p[2] > w-dx;
}

/* move to nearest non solid point along the trajectory */
void move_in(Eigen::Vector3d &curr_p, Eigen::Vector3d prev_pos){
    Eigen::Vector3d trajectory = prev_pos - curr_p;
    std::vector<double> potential_t;
    // handle paralle line
    if (trajectory[0] != 0){
        potential_t.push_back((-l+dx - curr_p[0]) / trajectory[0]);
        potential_t.push_back((l - dx - curr_p[0])/ trajectory[0]);
    }
    if (trajectory[1] != 0){
        potential_t.push_back((-h+dx - curr_p[1]) / trajectory[1]);
        potential_t.push_back((3*h - curr_p[1]) / trajectory[1]);
    }
    if (trajectory[2] != 0){
        potential_t.push_back((-w+dx - curr_p[2])/trajectory[2]);
        potential_t.push_back((w - dx - curr_p[2])/ trajectory[2]);
    }
    
    for (int i = 0; i < potential_t.size(); i++){
        if (potential_t[i] > 0 && potential_t[i] <= 1.0){
            curr_p += potential_t[i] * trajectory;
            break;
        }
    }
}


/*
 Runge kutta integration =====================================================
 */
Eigen::Vector3d RK4(Util::MacGrid mac_grid, Eigen::Vector3d pos, double dt){
    Eigen::Vector3d v1 = getVel(mac_grid, pos, false);
    Eigen::Vector3d v2 = getVel(mac_grid, pos + 0.5 * dt * v1, false);
    Eigen::Vector3d v3 = getVel(mac_grid, pos + 0.75 * dt * v2, false);
    return pos + dt * (2./9 * v1 + 1./3 * v2 + 4./9 * v3);
}

Eigen::Vector3d backward_RK4(Util::MacGrid mac_grid, Eigen::Vector3d pos, double dt){
    Eigen::Vector3d v1 = getVel(mac_grid, pos);
    Eigen::Vector3d v2 = getVel(mac_grid, pos + 0.5 * dt * v1);
    Eigen::Vector3d v3 = getVel(mac_grid, pos + 0.75 * dt * v2);
    return pos - dt * (2./9 * v1 + 1./3 * v2 + 4./9 * v3);
}


/*
 Pressure Solver ==========================================================
 */
void update_pressure(Util::MacGrid &mac_grid, std::vector<double> result, std::vector<int> fluid_indices){
    mac_grid.p.setZero();
    for (int i = 0; i < fluid_indices.size(); i++) {
        int target = fluid_indices[i];
        mac_grid.p[target] = result[i];
    }
}

/* update velocity in grids according to new pressure */
void update_vel(Util::MacGrid &mac_grid, double dt){
    for (int i = 1; i < n_x; i++) {
        for (int j = 1; j < n_y; j++) {
            for (int k = 1; k < n_z; k++) {
                int curr = (n_z*n_y)*i+n_z*j+k;
                int prevx = (n_z*n_y)*(i-1)+n_z*j+k;
                int prevy = (n_z*n_y)*i+n_z*(j-1)+k;
                int prevz = (n_z*n_y)*i+n_z*j+k-1;
                // equation (4.6)
                if (mac_grid.label[curr] == 2 || mac_grid.label[prevx] == 2){
                    mac_grid.vx[getIndexIntoVx(i, j, k)] -= dt * (mac_grid.p[curr]-mac_grid.p[prevx]) / density / dx;
                }
                if (mac_grid.label[curr] == 2 || mac_grid.label[prevy] == 2){
                    mac_grid.vy[getIndexIntoVy(i, j, k)] -= dt * (mac_grid.p[curr]-mac_grid.p[prevy]) / density / dx;
                }
                if (mac_grid.label[curr] == 2 || mac_grid.label[prevz] == 2){
                    mac_grid.vz[getIndexIntoVz(i, j, k)] -= dt * (mac_grid.p[curr]-mac_grid.p[prevz]) / density / dx;
                }
                
                // boundary condition
                if (mac_grid.label[curr] == 1){
                    mac_grid.vx[getIndexIntoVx(i, j, k)] = 0.;
                    mac_grid.vy[getIndexIntoVy(i, j, k)] = 0.;
                    mac_grid.vz[getIndexIntoVz(i, j, k)] = 0.;
                }
                if (mac_grid.label[prevx] == 1){
                    mac_grid.vx[getIndexIntoVx(i, j, k)] = 0.;
                }
                if (mac_grid.label[prevy] == 1){
                    mac_grid.vy[getIndexIntoVy(i, j, k)] = 0.;
                }
                if (mac_grid.label[prevz] == 1){
                    mac_grid.vz[getIndexIntoVz(i, j, k)] = 0.;
                }
            }
        }
    }
}

/* get the number of non solid (air/fluid) neighbours */
int getNumNonSolid(Util::MacGrid mac_grid, int i) {
    Eigen::VectorXi label = mac_grid.label;

    int n = 0;
    if (label[i - n_y*n_z] != 1) { n++; }
    if (label[i + n_y*n_z] != 1) { n++; }
    if (label[i - n_z] != 1) { n++; }
    if (label[i + n_z] != 1) { n++; }
    if (label[i - 1] != 1) { n++; }
    if (label[i + 1] != 1) { n++; }
    
    return n;
}

/* get the matrix A of Ap=d described in equation (4.25) */
void getMatrix(Util::MacGrid &mac_grid, std::vector<int> fluid_indices, std::unordered_map<int, int> fluid_cell_map, SparseMatrixd &matrix, double dt){
    matrix.zero();
    double scale = dt / density / dx / dx;
    for (int i = 0; i < fluid_indices.size(); i++) {
        int target = fluid_indices[i];
        std::vector<unsigned int> indices;
        std::vector<double> values;
        int n = getNumNonSolid(mac_grid, target);
        indices.push_back(i);
        values.push_back(scale * n);
        if (mac_grid.label[target - n_z*n_y] == 2) {
            // the index of target-1 in fluid_cells
            auto search = fluid_cell_map.find(target - n_z*n_y);
            int ind = search->second;
            indices.push_back(ind);
            values.push_back(-scale);
        }
        if (mac_grid.label[target - n_z] == 2) {
            auto search = fluid_cell_map.find(target - n_z);
            int ind = search->second;
            indices.push_back(ind);
            values.push_back(-scale);
        }
        if (mac_grid.label[target - 1] == 2) {
            auto search = fluid_cell_map.find(target - 1);
            int ind = search->second;
            indices.push_back(ind);
            values.push_back(-scale);
        }
        if (mac_grid.label[target + 1] == 2) {
            auto search = fluid_cell_map.find(target + 1);
            int ind = search->second;
            indices.push_back(ind);
            values.push_back(-scale);
        }
        if (mac_grid.label[target + n_z] == 2) {
            auto search = fluid_cell_map.find(target + n_z);
            int ind = search->second;
            indices.push_back(ind);
            values.push_back(-scale);
        }
        if (mac_grid.label[target + n_z*n_y] == 2) {
            auto search = fluid_cell_map.find(target + n_y*n_z);
            int ind = search->second;
            indices.push_back(ind);
            values.push_back(-scale);
        }
        matrix.add_sparse_row(i, indices, values);
    }
    
}

/* get the vector d of Ap=d described in equation (4.25) */
void getNegDivergence(Util::MacGrid &mac_grid, std::vector<Eigen::Vector3i> fluid_cells, std::vector<double> &rhs){
    double scale = 1. / dx;
    for (int i = 0; i < fluid_cells.size(); i++) {
        int a = fluid_cells[i][0];
        int b = fluid_cells[i][1];
        int c = fluid_cells[i][2];
        int target = (n_y*n_z)*a+n_z*b+c;
        int x = getIndexIntoVx(a, b, c);
        int y = getIndexIntoVy(a, b, c);
        int z = getIndexIntoVz(a, b, c);
        double di = scale * (mac_grid.vx[x+n_y*n_z] - mac_grid.vx[x] +
                             mac_grid.vy[y+n_z] - mac_grid.vy[y] +
                             mac_grid.vz[z+1] - mac_grid.vz[z]);
        rhs.push_back(-di);
        // solid boundary cond (4.24 p31)
        // solid vel = 0.
        if (mac_grid.label[target - n_y*n_z] == 1) {
            rhs[i] -= scale*(mac_grid.vx[x] - 0.);
        }
        if (mac_grid.label[target + n_y*n_z] == 1) {
            rhs[i] += scale*(mac_grid.vx[x + n_y*n_z] - 0.);
        }
        
        if (mac_grid.label[target - n_z] == 1) {
            rhs[i] -= scale*(mac_grid.vy[y] - 0.);
        }
        if (mac_grid.label[target + n_z] == 1) {
            rhs[i] += scale*(mac_grid.vy[y + n_z] - 0.);
        }
        
        if (mac_grid.label[target - 1] == 1) {
            rhs[i] -= scale*(mac_grid.vz[z] - 0.);
        }
        if (mac_grid.label[target + 1] == 1) {
            rhs[i] += scale*(mac_grid.vz[z + 1] - 0.);
        }
    }
}

/* project use PCG pressure solver */
void project(Util::MacGrid &mac_grid, Util::MarkerParticle &marker_particles, double dt, std::vector<int> fluid_indices, std::vector<Eigen::Vector3i> fluid_cells){
    PCGSolver<double> solver;
    std::unordered_map<int, int> fluid_cell_map;
    for (int i = 0; i < fluid_indices.size(); i++){
        // index into mac grid : index into fluid cells
        fluid_cell_map.insert({fluid_indices[i], i});
    }
    SparseMatrixd matrix(fluid_indices.size(), 7);
    getMatrix(mac_grid, fluid_indices, fluid_cell_map, matrix, dt);
    std::vector<double> rhs;
    getNegDivergence(mac_grid, fluid_cells, rhs);
    
    std::vector<double> result;
    result.resize(fluid_indices.size());
    double residual_out;
    int iterations_out;
    solver.solve(matrix, rhs, result, residual_out, iterations_out);
    update_pressure(mac_grid, result, fluid_indices);
    update_vel(mac_grid, dt);
    
}


/*
 Advection: semi lagrange and flip =====================================================
 */
void advectX_sl(Util::MacGrid &mac_grid, double dt){
    for (int i = 1; i < n_x; i++){
        for(int j = 1; j < n_y; j++){
            for (int k = 1; k < n_z - 1; k++){
                // calculate current pos
                // handle double error
                Eigen::Vector3d curr_p = Eigen::Vector3d(-10*l, -10*h+5 * dx, -10*w+5 * dx) + Eigen::Vector3d(10*i*dx, 10*j*dx, 10*k*dx);
                curr_p /= 10.;
                Eigen::Vector3d prev_p = backward_RK4(mac_grid, curr_p, dt);
                if(inSolid(prev_p)){
                    move_in(prev_p, curr_p);
                }
                mac_grid.vx[(n_y*n_z)*i+n_z*j+k] = getVel(mac_grid, prev_p)[0];
            }
        }
    }
}

void advectY_sl(Util::MacGrid &mac_grid, double dt){
    for (int i = 1; i < n_x - 1; i++){
        for(int j = 1; j < n_y + 1; j++){
            for (int k = 1; k < n_z - 1; k++){
                Eigen::Vector3d curr_p = Eigen::Vector3d(-10*l+5 * dx, -10*h, -10*w+5 * dx) + Eigen::Vector3d(10*i*dx, 10*j*dx, 10*k*dx);
                curr_p /= 10.;
                Eigen::Vector3d prev_p = backward_RK4(mac_grid, curr_p, dt);
                if(inSolid(prev_p)){
                    move_in(prev_p, curr_p);
                }
                mac_grid.vy[((n_y+1)*n_z)*i+n_z*j+k] = getVel(mac_grid, prev_p)[1];
            }
        }
    }
}

void advectZ_sl(Util::MacGrid &mac_grid, double dt){
    for (int i = 1; i < n_x - 1; i++){
        for(int j = 1; j < n_y; j++){
            for (int k = 1; k < n_z; k++){
                Eigen::Vector3d curr_p = Eigen::Vector3d(-10*l+5 * dx, -10*h+5 *dx, -10*w) + Eigen::Vector3d(10*i*dx, 10*j*dx, 10*k*dx);
                curr_p /= 10.;
                Eigen::Vector3d prev_p = backward_RK4(mac_grid, curr_p, dt);
                if(inSolid(prev_p)){
                    move_in(prev_p, curr_p);
                }
                mac_grid.vz[(n_y*(n_z+1))*i+(n_z+1)*j+k] = getVel(mac_grid, prev_p)[2];
            }
        }
    }
}

void advect_sl(Util::MacGrid &mac_grid, double dt){
    advectX_sl(mac_grid, dt);
    advectY_sl(mac_grid, dt);
    advectZ_sl(mac_grid, dt);
}

/* extrapolate the velocity from marker particle if flip advection is used */
void extrapolate(){
    
}

// TODO
void advect_flip(Util::MacGrid &mac_grid, double dt){
    //advectX_flip(mac_grid, dt);
    //advectY_flip(mac_grid, dt);
    //advectZ_flip(mac_grid, dt);
    
}


/*
 Body forces ==========================================================
 */
void body_forces(double dt, Util::MacGrid &mac_grid, std::vector<Eigen::Vector3i> fluid_cells){
    // apply gravity to fluid cells
    for (int i = 1; i < n_x-1; i++) {
        for (int j = 1; j < n_y; j++) {
            for (int k = 1; k < n_z-1; k++) {
                int curr = (n_z*n_y)*i+n_z*j+k;
                int prevy = (n_z*n_y)*i+n_z*(j-1)+k;
                // equation (4.6)
                if (mac_grid.label[curr] == 2 || mac_grid.label[prevy] == 2){
                    mac_grid.vy[getIndexIntoVy(i, j, k)] += dt * g;
                }
            }
        }
    }
}


/*
 marker particle update ==================================================
 */
void update_marker_particles(Util::MacGrid &mac_grid, Util::MarkerParticle &marker_particles, double dt){
    int n = marker_particles.gridIndex.size();
    for (int i = 0; i < n; i++) {
        // update pos
        Eigen::Vector3d curr_p = RK4(mac_grid, marker_particles.pos.row(i), dt);
        // out of non solid range
        if (inSolid(curr_p)){
            move_in(curr_p, marker_particles.pos.row(i));
        }
        marker_particles.pos.row(i) = curr_p;
        
        int a = (int)floor((curr_p[0]+l) / dx);
        int b = (int)floor((curr_p[1]+h) / dx);
        int c = (int)floor((curr_p[2]+w) / dx);
        marker_particles.gridIndex[i] = (n_y*n_z)*a+n_z*b+c;
    }
}

// TODO: flip advection
void update_marker_particles_flip(){
    // update vel
}


/*
 Label update ====================================================================
 */
void update_label(Util::MacGrid &mac_grid, Util::MarkerParticle &marker_particles, std::vector<Eigen::Vector3i> &fluid_cells, std::vector<int> &fluid_indices){
    fluid_indices.clear();
    fluid_cells.clear();
    std::vector<int> vec = marker_particles.gridIndex;
    
    for (int i = 1; i < n_x - 1; i++) {
        for (int j = 1; j < n_y; j++) {
            for (int k = 1; k < n_z - 1; k++) {
                int target = (n_z*n_y)*i+n_z*j+k;
                if (std::find(vec.begin(), vec.end(), target) != vec.end()){
                    // fluid cell
                    mac_grid.label[target] = 2;
                    fluid_cells.push_back(Eigen::Vector3i(i, j, k));
                    fluid_indices.push_back(target);
                }else{
                    mac_grid.label[target] = 0;
                }
            }
        }
    }
}


/*
 Time step ==================================================================
 */
double calculate_dt(Util::MacGrid mac_grid, Util::MarkerParticle &marker_particles, std::vector<Eigen::Vector3i> fluid_cells){
    double u_max = sqrt(-5*g*dx);
    for (int n = 0; n<fluid_cells.size();n++){
        int i = fluid_cells[n][0];
        int j = fluid_cells[n][1];
        int k = fluid_cells[n][2];
        
        int ix = getIndexIntoVx(i, j, k);
        int iy = getIndexIntoVy(i, j, k);
        int iz = getIndexIntoVz(i, j, k);
        Eigen::Vector3d vel = Eigen::Vector3d((mac_grid.vx[ix] + mac_grid.vx[ix+n_y*n_z])/2.,
                                              (mac_grid.vy[iy] + mac_grid.vy[iy+n_z])/2.,
                                              (mac_grid.vz[iz] + mac_grid.vz[iz+1])/2.);
        if (vel.norm() > u_max){
            u_max = vel.norm() + sqrt(-5*g*dx);
        }
    }
    double dt = 5. * dx / u_max;
    
    // 0.1 by experiment
    if (dt > 0.1){
        return 0.1;
    }
    
    return dt;
}


/*
 Main simulation loop
 */
inline void simulate(Util::MacGrid &mac_grid, Util::MarkerParticle &marker_particles){
    if (!simulation_pause){
        std::vector<Eigen::Vector3i> fluid_cells;
        std::vector<int> fluid_indices;
        update_label(mac_grid, marker_particles, fluid_cells, fluid_indices);
        double dt = calculate_dt(mac_grid, marker_particles, fluid_cells);
        // semi lagrangian advection
        advect_sl(mac_grid, dt);
        // apply gravity
        body_forces(dt, mac_grid, fluid_cells);
        // incompressibility, boundary cndition
        project(mac_grid, marker_particles, dt, fluid_indices, fluid_cells);
        update_marker_particles(mac_grid, marker_particles, dt);
        t += dt;
        iter++;
    }
}


/*
 Visualization
 */
inline void draw(Util::MarkerParticle marker_particles){
    Visualize::update_particle_positions(marker_particles.pos);
}

#endif

