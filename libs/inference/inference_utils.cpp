

#include "inference_utils.hpp"

#include <vector>
#include <queue>

using namespace UPGMpp;
using namespace std;
using namespace Eigen;

size_t UPGMpp::messagesLBP(CGraph &graph,
                            TInferenceOptions &options,
                            vector<vector<VectorXd> > &messages ,
                            bool maximize )
{
    const vector<CNodePtr> nodes = graph.getNodes();
    const vector<CEdgePtr> edges = graph.getEdges();
    multimap<size_t,CEdgePtr> edges_f = graph.getEdgesF();

    size_t N_nodes = nodes.size();
    size_t N_edges = edges.size();

    graph.computePotentials();

    //
    // Build the messages structure
    //

    double totalSumOfMsgs = 0;

    messages.resize( N_edges);

    for ( size_t i = 0; i < N_edges; i++ )
    {
        messages[i].resize(2);

        size_t ID1, ID2;
        edges[i]->getNodesID(ID1,ID2);

        // Messages from first node of the edge to the second one, so the size of
        // the message has to be the same as the number of classes of the second node.
        messages[i][0].resize( graph.getNodeWithID( ID2 )->getPotentials( options.considerNodeFixedValues ).rows() );
        messages[i][0].fill(1);
        // Just the opposite as before.
        messages[i][1].resize( graph.getNodeWithID( ID1 )->getPotentials( options.considerNodeFixedValues ).rows() );
        messages[i][1].fill(1);

        totalSumOfMsgs += messages[i][0].rows() + messages[i][1].rows();

    }

    //
    // Iterate until convergence or a certain maximum number of iterations is reached
    //

    size_t iteration;

    for ( iteration = 0; iteration < options.maxIterations; iteration++ )
    {
        //
        // Iterate over all the nodes
        //
        for ( size_t nodeIndex = 0; nodeIndex < N_nodes; nodeIndex++ )
        {
            const CNodePtr nodePtr = graph.getNode( nodeIndex );
            size_t nodeID          = nodePtr->getID();

            pair<multimap<size_t,CEdgePtr>::iterator,multimap<size_t,CEdgePtr>::iterator > neighbors;

            neighbors = edges_f.equal_range(nodeID);

            //
            // Send a message to each neighbor
            //
            for ( multimap<size_t,CEdgePtr>::iterator itNeigbhor = neighbors.first;
                  itNeigbhor != neighbors.second;
                  itNeigbhor++ )
            {
                VectorXd nodePotPlusIncMsg = nodePtr->getPotentials( options.considerNodeFixedValues);
                size_t neighborID;
                size_t ID1, ID2;
                CEdgePtr edgePtr( (*itNeigbhor).second );
                edgePtr->getNodesID(ID1,ID2);
                ( ID1 == nodeID ) ? neighborID = ID2 : neighborID = ID1;

                //
                // Compute the message from current node as a product of all the
                // incoming messages less the one from the current neighbor
                // plus the node potential of the current node.
                //
                for ( multimap<size_t,CEdgePtr>::iterator itNeigbhor2 = neighbors.first;
                      itNeigbhor2 != neighbors.second;
                      itNeigbhor2++ )
                {
                    size_t ID11, ID12;
                    CEdgePtr edgePtr2( (*itNeigbhor2).second );
                    edgePtr2->getNodesID(ID11,ID12);
                    size_t edgeIndex = graph.getEdgeIndex( edgePtr2->getID() );

                    // Check if the current neighbor appears in the edge
                    if ( ( neighborID != ID11 ) && ( neighborID != ID12 ) )
                    {
                        if ( nodeID == ID11 )
                            nodePotPlusIncMsg = nodePotPlusIncMsg.cwiseProduct(messages[ edgeIndex ][ 1 ]);
                        else // nodeID == ID2
                            nodePotPlusIncMsg = nodePotPlusIncMsg.cwiseProduct(messages[ edgeIndex ][ 0 ]);
                    }
                }

                //cout << "Node pot" << nodePotPlusIncMsg << endl;

                //
                // Take also the potential between the two nodes
                //
                MatrixXd edgePotentials;

                if ( nodeID != ID1 )
                    edgePotentials = edgePtr->getPotentials();
                else
                    edgePotentials = edgePtr->getPotentials().transpose();

                VectorXd newMessage;
                size_t edgeIndex = graph.getEdgeIndex( edgePtr->getID() );

                if ( !maximize )
                {
                    // Multiply both, and update the potential

                    newMessage = edgePotentials * nodePotPlusIncMsg;
                }
                else
                {

                    if ( nodeID == ID1 )
                        newMessage.resize(messages[ edgeIndex ][0].rows());
                    else
                        newMessage.resize(messages[ edgeIndex ][1].rows());

                    for ( size_t row = 0; row < edgePotentials.rows(); row++ )
                    {
                        double maxRowValue = std::numeric_limits<double>::min();

                        for ( size_t col = 0; col < edgePotentials.cols(); col++ )
                        {
                            double value = edgePotentials(row,col)*nodePotPlusIncMsg(col);
                            if ( value > maxRowValue )
                                maxRowValue = value;
                        }
                        newMessage(row) = maxRowValue;
                    }

                    // Normalize new message
                    newMessage = newMessage / newMessage.sum();

                    //cout << "New message: " << endl << newMessage << endl;
                }

                //
                // Set the message!
                //

                if ( nodeID == ID1 )
                    messages[ edgeIndex ][0] = newMessage;
                else
                    messages[ edgeIndex ][1] = newMessage;

            }

        } // Nodes

        //
        // Check convergency!!
        //

        double newTotalSumOfMsgs = 0;
        for ( size_t i = 0; i < N_edges; i++ )
        {
            newTotalSumOfMsgs += messages[i][0].sum() + messages[i][1].sum();
        }

        //printf("%4.10f\n",std::abs( totalSumOfMsgs - newTotalSumOfMsgs ));

        if ( std::abs( totalSumOfMsgs - newTotalSumOfMsgs ) <
             options.convergency )
            break;

        totalSumOfMsgs = newTotalSumOfMsgs;

    } // Iterations

    return 1;
}




// For-Fulkerson implemetation for max-flow min-cut computation adapted from:
// http://www.geeksforgeeks.org/ford-fulkerson-algorithm-for-maximum-flow-problem

// Number of vertices in given graph
//#define V 6

void getFinalCut(MatrixXd &rGraph, int s, int t, VectorXi &cut)
{
    size_t N_nodes = rGraph.cols();
    bool visited[N_nodes];
    memset(visited, 0, sizeof(visited));

    queue<int> q;
    q.push(s);

    cut(s) = 1;
    visited[s] = true;

    // Standard BFS Loop
    while (!q.empty())
    {
        int u = q.front();
        q.pop();

        for (int v=0; v<N_nodes; v++)
        {
            //cout << "Visiting from " << u << " to " << v << " residual " << rGraph(u,v) << endl;
            if (visited[v]==false && rGraph(u,v) > 0)
            {
                q.push(v);
                visited[v] = true;
                cut(v) = 1;
            }
        }
    }

}

/* Returns true if there is a path from source 's' to sink 't' in
  residual graph. Also fills parent[] to store the path */
bool bfs(MatrixXd &rGraph, int s, int t, int parent[])
{
    size_t N_nodes = rGraph.cols();
    // Create a visited array and mark all vertices as not visited
    bool visited[N_nodes];
    memset(visited, 0, sizeof(visited));

    // Create a queue, enqueue source vertex and mark source vertex
    // as visited
    queue <int> q;
    q.push(s);
    visited[s] = true;

    // Standard BFS Loop
    while (!q.empty())
    {
        int u = q.front();
        q.pop();

        for (int v=0; v<N_nodes; v++)
        {
            if (visited[v]==false && rGraph(u,v) > 0)
            {
                q.push(v);
                parent[v] = u;
                visited[v] = true;
            }
        }
    }

    // If we reached sink in BFS starting from source, then return
    // true, else false
    return (visited[t] == true);
}

// Returns tne maximum flow from s to t in the given graph
int UPGMpp::fordFulkerson(MatrixXd &graph, int s, int t, VectorXi &cut)
{
    size_t N_nodes = graph.cols();
    int u, v;

    // Create a residual graph and fill the residual graph with
    // given capacities in the original graph as residual capacities
    // in residual graph
    MatrixXd rGraph; // Residual graph where rGraph[i][j] indicates
    rGraph.resize(N_nodes,N_nodes);
                     // residual capacity of edge from i to j (if there
                     // is an edge. If rGraph[i][j] is 0, then there is not)
    for (u = 0; u < N_nodes; u++)
        for (v = 0; v < N_nodes; v++)
             rGraph(u,v) = graph(u,v);

    int parent[N_nodes];  // This array is filled by BFS and to store path

    double max_flow = 0;  // There is no flow initially



    // Augment the flow while tere is path from source to sink
    while (bfs(rGraph, s, t, parent))
    {
        // Find minimum residual capacity of the edhes along the
        // path filled by BFS. Or we can say find the maximum flow
        // through the path found.
        double path_flow = std::numeric_limits<double>::max();
        for (v=t; v!=s; v=parent[v])
        {
            u = parent[v];
            path_flow = min(path_flow, rGraph(u,v));
        }

        //cout << "Path flow: " << path_flow;

        // update residual capacities of the edges and reverse edges
        // along the path
        for (v=t; v != s; v=parent[v])
        {
            u = parent[v];
            rGraph(u,v) -= path_flow;
            rGraph(v,u) += path_flow;
        }

        // Add path flow to overall flow
        max_flow += path_flow;
    }

    getFinalCut(rGraph,s,t,cut);

    cout << "The minimun cut is: ";

    for ( int i=0; i<N_nodes; i++ )
        cout << cut(i) << " ";

    cout << endl;

    // Return the overall flow
    return max_flow;
}
