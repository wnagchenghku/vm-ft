Say if the tree has `x` leaf (which means `x` pages in our project), then

	
	l =  int (log (x));
	
	//Tree height
	th = l + 1; 

	//Total number of all nodes
	nnodes = (2 ^ th - 1) + 2 * (x - 2 ^ l); 

	indices_of_leaf = [(nnodes-1)/2, ..., nnodes-1]  //index from 0

	childer_of_node (y) = (2*y +1, 2 *y +2)

mapping: 

	x[i] -> nodes[j]:
	i start from 0;
		if i < 2 *( x - 2 ^ l): j=  (2 ^ th - 2) + i + 1; 
		else: j = i - (2 *( x - 2 ^ l)) + (nnodes - 1)/2; 

	nodes[j] -> x[i]:
		if j > (2 ^ th - 2) : (j - (2 ^ th - 2)) 
		else: j - (nnodes-1/2) +1 