/**
 * This file contains an example of graphlab used for discrete loopy
 * belief propagation in a pairwise markov random field to denoise a
 * synthetic noisy image.
 *
 *  \author Joseph Gonzalez
 */

// INCLUDES ===================================================================>

// Including Standard Libraries
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <cmath>

#include <boost/program_options.hpp>


#include <graphlab.hpp>

#include "image.hpp"


// Include the macro for the for each operation
#include <graphlab/macros_def.hpp>


enum constants {EDGE_FACTOR_ID, BOUND_ID, DAMPING_ID};



// STRUCTS (Edge and Vertex data) =============================================>

/**
 * The data associated with each directed edge in the pairwise markov
 * random field
 */
struct edge_data {
  graphlab::unary_factor message;
  graphlab::unary_factor old_message;
}; // End of edge data


/**
 * The data associated with each variable in the pairwise markov
 * random field
 */
struct vertex_data {
  graphlab::unary_factor potential;
  graphlab::unary_factor belief;
}; // End of vertex data


typedef graphlab::graph<vertex_data, edge_data> graph_type;
typedef graphlab::types<graph_type> gl_types;


// GraphLab Update Function ===================================================>

/** Construct denoising ising model based on the image */
void construct_graph(image& img,
                     size_t num_rings,
                     double sigma,
                     gl_types::graph& graph);

/** 
 * The core belief propagation update function.  This update satisfies
 * the graphlab update_function interface.  
 */
void bp_update(gl_types::iscope& scope, 
               gl_types::icallback& scheduler,
               gl_types::ishared_data* shared_data);
               

// Command Line Parsing =======================================================>

struct options {
  size_t ncpus;
  double bound;
  double damping;
  size_t num_rings;
  size_t rows;
  size_t cols;
  double sigma;
  double lambda;
  size_t splash_size;
  std::string smoothing;
  std::string engine;
  std::string scope;
  std::string scheduler;
  std::string orig_fn;
  std::string noisy_fn;
  std::string pred_fn;
  std::string pred_type;
  std::string visualizer;
  std::string partmethod;
  size_t clustersize;
};


// MAIN =======================================================================>
int main(int argc, char** argv) {
  std::cout << "This program creates and denoises a synthetic " << std::endl
            << "image using loopy belief propagation inside " << std::endl
            << "the graphlab framework." << std::endl;

  // set the global logger
  global_logger().set_log_level(LOG_WARNING);
  global_logger().set_log_to_console(true);


  double bound = 1E-15;
  double damping = 0.1;
  size_t colors = 5;
  size_t rows = 200;
  size_t cols = 200;
  double sigma = 2;
  double lambda = 10;
  std::string smoothing = "laplace";
  std::string orig_fn = "source_img.pgm";
  std::string noisy_fn = "noisy_img.pgm";
  std::string pred_fn = "pred_img.pgm";
  std::string pred_type = "map";




  // Parse command line arguments --------------------------------------------->
  graphlab::command_line_options clopts("Loopy BP image denoising");
  clopts.attach_option("bound",
                       &bound, bound,
                       "Residual termination bound");
  clopts.attach_option("damping",
                       &damping, damping,
                       "The ammount of message damping");
  clopts.attach_option("colors",
                       &colors, colors,
                       "The number of colors in the noisy image");
  clopts.attach_option("rows",
                       &rows, rows,
                       "The number of rows in the noisy image");
  clopts.attach_option("cols",
                       &cols, cols,
                       "The number of columns in the noisy image");
  clopts.attach_option("sigma",
                       &sigma, sigma,
                       "Standard deviation of noise.");
  clopts.attach_option("lambda",
                       &lambda, lambda,
                       "Smoothness parameter (larger => smoother).");
  clopts.attach_option("smoothing",
                       &smoothing, smoothing,
                       "Options are {square, laplace}");
  clopts.attach_option("orig",
                       &orig_fn, orig_fn,
                       "Original image file name.");
  clopts.attach_option("noisy",
                       &noisy_fn, noisy_fn,
                       "Noisy image file name.");
  clopts.attach_option("pred",
                       &pred_fn, pred_fn,
                       "Predicted image file name.");
  clopts.attach_option("pred_type",
                       &pred_type, pred_type,
                       "Predicted image type {map, exp}");
  

  clopts.scheduler_type = "splash(100)";
  clopts.scope_type = "edge";
  

  bool success = clopts.parse(argc, argv);
  if(!success) {    
    return EXIT_FAILURE;
  }


  
  std::cout << "ncpus:          " << clopts.ncpus << std::endl
            << "bound:          " << bound << std::endl
            << "damping:        " << damping << std::endl
            << "colors:         " << colors << std::endl
            << "rows:           " << rows << std::endl
            << "cols:           " << cols << std::endl
            << "sigma:          " << sigma << std::endl
            << "lambda:         " << lambda << std::endl
            << "smoothing:      " << smoothing << std::endl
            << "engine:         " << clopts.engine_type << std::endl
            << "scope:          " << clopts.scope_type << std::endl
            << "scheduler:      " << clopts.scheduler_type << std::endl
            << "orig_fn:        " << orig_fn << std::endl
            << "noisy_fn:       " << noisy_fn << std::endl
            << "pred_fn:        " << pred_fn << std::endl
            << "pred_type:      " << pred_type << std::endl;

  
  

  // Create synthetic images -------------------------------------------------->
  // Creating image for denoising
  std::cout << "Creating a synethic image. " << std::endl;
  image img(rows, cols);
  img.paint_sunset(colors);
  std::cout << "Saving image. " << std::endl;
  img.save(orig_fn.c_str());
  std::cout << "Corrupting Image. " << std::endl;
  img.corrupt(sigma);
  std::cout << "Saving corrupted image. " << std::endl;
  img.save(noisy_fn.c_str());


 
  
  
  // Create the graph --------------------------------------------------------->
  gl_types::core core;
  // Set the engine options
  core.set_engine_options(clopts);
  
  std::cout << "Constructing pairwise Markov Random Field. " << std::endl;
  construct_graph(img, colors, sigma, core.graph());

  
  // Setup global shared variables -------------------------------------------->
  // Initialize the edge agreement factor 
  std::cout << "Initializing shared edge agreement factor. " << std::endl;

  // dummy variables 0 and 1 and num_rings by num_rings
  graphlab::binary_factor edge_potential(0, colors, 0, colors);
  // Set the smoothing type
  if(smoothing == "square") {
    edge_potential.set_as_agreement(lambda);
  } else if (smoothing == "laplace") {
    edge_potential.set_as_laplace(lambda);
  } else {
    std::cout << "Invalid smoothing stype!" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << edge_potential << std::endl;
  
  core.shared_data().set_constant(EDGE_FACTOR_ID, graphlab::any(edge_potential));
  core.shared_data().set_constant(BOUND_ID, graphlab::any(bound));
  core.shared_data().set_constant(DAMPING_ID, graphlab::any(damping));
  


  // Running the engine ------------------------------------------------------->
  core.scheduler().set_option(gl_types::scheduler_options::UPDATE_FUNCTION,
                              (void*)bp_update);

  std::cout << "Running the engine. " << std::endl;

  
  // Add the bp update to all vertices
  core.add_task_to_all(bp_update, 100.0);
  // Starte the engine
  double runtime = core.start();
  
  size_t update_count = core.last_update_count();
  std::cout << "Finished Running engine in " << runtime 
            << " seconds." << std::endl
            << "Total updates: " << update_count << std::endl
            << "Efficiency: " << (double(update_count) / runtime)
            << " updates per second "
            << std::endl;  


  // Saving the output -------------------------------------------------------->
  std::cout << "Rendering the cleaned image. " << std::endl;
  if(pred_type == "map") {
    for(size_t v = 0; v < core.graph().num_vertices(); ++v) {
      const vertex_data& vdata = core.graph().vertex_data(v);
      img.pixel(v) = vdata.belief.max_asg();    
    }
  } else if(pred_type == "exp") {
    for(size_t v = 0; v < core.graph().num_vertices(); ++v) {
      const vertex_data& vdata = core.graph().vertex_data(v);
      img.pixel(v) = vdata.belief.expectation();
    }
  } else {
    std::cout << "Invalid prediction type! : " << pred_type
              << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Saving cleaned image. " << std::endl;
  img.save(pred_fn.c_str());

  std::cout << "Done!" << std::endl;
  return EXIT_SUCCESS;
} // End of main




// Implementations
// ============================================================>
void bp_update(gl_types::iscope& scope, 
               gl_types::icallback& scheduler,
               gl_types::ishared_data* shared_data) {
  //  std::cout << scope.vertex();;
  //  std::getchar();
  assert(shared_data != NULL);

  // Get the shared data
  double bound = shared_data->get_constant(BOUND_ID).as<double>();
  double damping = shared_data->get_constant(DAMPING_ID).as<double>();

  // Grab the state from the scope
  // ---------------------------------------------------------------->
  // Get the vertex data
  vertex_data& v_data = scope.vertex_data();
  
  // Get the in and out edges by reference
  const std::vector<graphlab::edge_id_t>& in_edges = 
    scope.in_edge_ids();
  const std::vector<graphlab::edge_id_t>& out_edges = 
    scope.out_edge_ids();
  assert(in_edges.size() == out_edges.size()); // Sanity check

  // Flip the old and new messages to improve safety when using the
  // unsynch scope
  foreach(graphlab::edge_id_t ineid, in_edges) {   
    // Get the in and out edge data
    edge_data& in_edge = scope.edge_data(ineid);
    // Since we are about to receive the current message make it the
    // old message
    in_edge.old_message = in_edge.message;
  }

  // Compute the belief
  // ---------------------------------------------------------------->
  // Initialize the belief as the value of the factor
  v_data.belief = v_data.potential;
  foreach(graphlab::edge_id_t ineid, in_edges) {
    // Get the message
    const edge_data& e_data = scope.edge_data(ineid);
    // Notice we now use the old message since neighboring vertices
    // could be changing the new messages
    v_data.belief.times( e_data.old_message );
  }
  v_data.belief.normalize(); // finally normalize the belief
  
  // Compute outbound messages
  // ---------------------------------------------------------------->

  const graphlab::binary_factor edge_factor =
    shared_data->get_constant(EDGE_FACTOR_ID).as<graphlab::binary_factor>();
  
  
  // Send outbound messages
  graphlab::unary_factor cavity, tmp_msg;
  for(size_t i = 0; i < in_edges.size(); ++i) {
    // Get the edge ids
    graphlab::edge_id_t outeid = out_edges[i];
    graphlab::edge_id_t ineid = in_edges[i];
    // CLEVER HACK: Here we are expoiting the sorting of the edge ids
    // to do fast O(1) time edge reversal
    assert(scope.target(outeid) == scope.source(ineid));
    // Get the in and out edge data
    const edge_data& in_edge = scope.edge_data(ineid);
    edge_data& out_edge = scope.edge_data(outeid);
    
    // Compute cavity
    cavity = v_data.belief;
    cavity.divide(in_edge.old_message); // Make the cavity a cavity
    cavity.normalize();


    // convolve cavity with the edge factor storing the result in the
    // temporary message
    tmp_msg.resize(out_edge.message.arity());
    tmp_msg.var() = out_edge.message.var();
    tmp_msg.convolve(edge_factor, cavity);
    tmp_msg.normalize();

    // Damp the message
    tmp_msg.damp(out_edge.message, damping);
    
    // Compute message residual
    double residual = tmp_msg.residual(out_edge.old_message);
    
    // Assign the out message
    out_edge.message = tmp_msg;
    
    if(residual > bound) {
      gl_types::update_task task(scope.target(outeid), bp_update);      
      scheduler.add_task(task, residual);
    }    
  }
} // end of BP_update


void construct_graph(image& img,
                     size_t num_rings,
                     double sigma,
                     gl_types::graph& graph) {
  // Construct a single blob for the vertex data
  vertex_data vdata;
  vdata.potential.resize(num_rings);
  vdata.belief.resize(num_rings);
  vdata.belief.uniform();
  vdata.potential.uniform();
  vdata.belief.normalize();
  vdata.potential.normalize();
  // Add all the vertices
  double sigmaSq = sigma*sigma;
  for(size_t i = 0; i < img.rows(); ++i) {
    for(size_t j = 0; j < img.cols(); ++j) {
      // initialize the potential and belief
      uint32_t pixel_id = img.vertid(i, j);
      vdata.potential.var() = vdata.belief.var() = pixel_id;
      // Set the node potential
      double obs = img.pixel(i, j);
      for(size_t pred = 0; pred < num_rings; ++pred) {
        vdata.potential.logP(pred) = 
          -(obs - pred)*(obs - pred) / (2.0 * sigmaSq);
      }
      vdata.potential.normalize();
      // Store the actual data in the graph
      size_t vertid = graph.add_vertex(vdata);
      // Ensure that we are using a consistent numbering
      assert(vertid == img.vertid(i, j));
    } // end of for j in cols
  } // end of for i in rows

  // Add the edges
  edge_data edata;
  edata.message.resize(num_rings);
  edata.message.uniform();
  edata.message.normalize();
  edata.old_message = edata.message;
  
  for(size_t i = 0; i < img.rows(); ++i) {
    for(size_t j = 0; j < img.cols(); ++j) {
      size_t vertid = img.vertid(i,j);
      if(i-1 < img.rows()) {
        edata.message.var() = img.vertid(i-1, j);
        edata.old_message.var() = edata.message.var();
        graph.add_edge(vertid, img.vertid(i-1, j), edata);
      }
      if(i+1 < img.rows()) {
        edata.message.var() = img.vertid(i+1, j);
        edata.old_message.var() = edata.message.var();
        graph.add_edge(vertid, img.vertid(i+1, j), edata);
      }
      if(j-1 < img.cols()) {
        edata.message.var() = img.vertid(i, j-1);
        edata.old_message.var() = edata.message.var();
        graph.add_edge(vertid, img.vertid(i, j-1), edata);
      } if(j+1 < img.cols()) {
        edata.message.var() = img.vertid(i, j+1);
        edata.old_message.var() = edata.message.var();
        graph.add_edge(vertid, img.vertid(i, j+1), edata);
      }
    } // end of for j in cols
  } // end of for i in rows
  graph.finalize();  
} // End of construct graph


