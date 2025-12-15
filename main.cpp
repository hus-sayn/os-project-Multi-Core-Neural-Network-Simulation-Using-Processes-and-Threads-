#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cstring>
#include <cstdlib>
#include <semaphore.h>
#include <sstream>

using namespace std;

// Global variables
#define num_of_neurons 100
#define MAX_LAYERS 20




double inputs[num_of_neurons];
double weights[num_of_neurons][num_of_neurons];
double outputs[num_of_neurons];
int numNeurons;
int numInputs;
pthread_mutex_t mutex_lock;
sem_t sem;
ofstream outputFile;





// Thread data passed via index
int thread_ka_Index[num_of_neurons];
double thread_ki_Input[num_of_neurons];
double thread_ka_Weights[num_of_neurons][num_of_neurons];
double threadOutputs[num_of_neurons];
int threadNumInputs;







// Thread function for neuron computation
// Ye thread function ek single neuron ka weighted sum calculate karta hai.
// Neuron ka index arg (idx) ke through milta hai. Is neuron ke liye yeh yeh kaam karta hai:
// 1) Semaphore ka wait karta hai taa-ke shared input aur weight arrays ko safely read kar sake.
// 2) Saare inputs (0 se threadNumInputs-1 tak) par loop chalata hai, har input value
// (thread_ki_Input[i]) ko is neuron ke mutalliq weight (thread_ka_Weights[idx][i]) se
// multiply karta hai aur un sab ka sum jama karta hai.
// 3) Mutex lock karta hai taa-ke computed sum ko shared threadOutputs[idx] array me likhte waqt
// kisi aur thread ke sath race condition na ho.
// 4) Mutex unlock karta hai, semaphore ko post karta hai, aur phir NULL return karke thread
// execution khatam kar deta hai

void* neuron_computation(void* arg) {
    int idx = *((int*)arg);

    double sum = 0.0;

    sem_wait(&sem);
    for (int i = 0; i < threadNumInputs; i++) {
        sum += thread_ki_Input[i] * thread_ka_Weights[idx][i];
    }

    pthread_mutex_lock(&mutex_lock);
    threadOutputs[idx] = sum;
    pthread_mutex_unlock(&mutex_lock);
    sem_post(&sem);

    return NULL;
}








// Function to read a line of comma-separated doubles

// Ye function file se ek line read karta hai jo comma-separated double values par  hai,
// in values ko parse karke double array me store karta hai, aur kitni values read hui hain woh return karta hai.
// 1) Pehle file se ek poori line getline(file, line) ke zariye read ki jati hai; agar line na mile
// (EOF ya error ho) to function 0 return kar deta hai.
// 2) Ek stringstream ss(line) banaya jata hai taa-ke is line ko asani se comma ke hisaab se tod kar
// alag-alag tokens (numbers) me convert kiya ja sake.
// 3) while loop ke andar getline(ss, token, ',') har dafa comma tak ka part nikalta hai, jab tak
// ya to saare tokens khatam na ho jayein ya count maxSize se chhota ho.
// 4) Har token ko atof(token.c_str()) se double me convert kiya jata hai aur arr[count] me store
// kiya jata hai, phir count++ kar diya jata hai taa-ke next value next index par aaye.
// 5) Jab saari possible values read ho jati hain, function count return karta hai jo batata hai
// ke us line se kitne doubles successfully parse kiye gaye
int readLine(ifstream& file, double* arr, int maxSize) {
    string line;
    if (!getline(file, line)) return 0;

    int count = 0;
    stringstream ss(line);
    string token;

    while (getline(ss, token, ',') && count < maxSize) {
        arr[count++] = atof(token.c_str());
    }
    return count;
}

// Function to perform forward pass for a layer



// Ye function ek poori layer ka forward pass perform karta hai, jahan har neuron ko
// alag thread ke through parallel taur par compute kiya jata hai.​
// Parameters:
// - layerNum: kis layer (number) ka forward pass ho raha hai (debug/printing ke liye).
// - layerInputs: is layer ke saare input values ka array.
// - numIn: inputs ki total tadad (kitne inputs har neuron ko mil rahe hain).
// - layerWeights: is layer ke neurons ke weights, jahan har neuron ke liye ek row hai.
// - neuronsInLayer: is layer me kitne neurons maujood hain.
// - layerResult: array jisme har neuron ka output store kiya jayega.
// - passNum: kaunsa forward pass hai (pehla, doosra, etc.) taa-ke output me show kar sakein.​
//
// Kaam ka tareeqa:
// 1) Sab se pehle, mutex lock karke saare inputs (layerInputs) ko global thread_ki_Input array
// me copy kiya jata hai, aur threadNumInputs me total input count store kiya jata hai.
// 2) Phir har neuron ke liye uske tamam weights (layerWeights[i][j]) ko global
// thread_ka_Weights array me copy kiya jata hai, taa-ke threads in shared arrays se
// data read kar saken. Saath hi thread_ka_Index[i] me neuron ka index save kiya jata hai.
// 3) Mutex unlock karne ke baad, har neuron ke liye ek thread banaya jata hai jo
// neuron_computation (neuronComput) function run karta hai, aur usay apna index pointer
// ( &thread_ka_Index[i] ) diya jata hai. Har thread apne neuron ka weighted sum calculate karega.
// 4) Phir ek loop me pthread_join use karke yeh ensure kiya jata hai ke saare threads apna
// computation complete kar lein, taa-ke saare neuronOutputs ready ho jayein.
// 5) Jab tamam threads khatam ho jate hain, threadOutputs array se har neuron ka result
// layerResult[i] me copy kar diya jata hai, jo is layer ka final output hota hai.
// 6) Aakhir me, cout ke through "Forward Pass X - Layer Y completed" ke sath tamam outputs
// print kiye jate hain, taa-ke console par clear ho ke nazar aaye ke is layer ka forward
// pass successfully complete ho gaya hai aur har neuron ka output kya hai.​




void forwardPass(int layerNum, double* layerInputs, int numIn, double layerWeights[][num_of_neurons], 
                 int neuronsInLayer, double* layerResult, int passNum) {

    pthread_t threads[num_of_neurons];

    // Copy data to global thread arrays
    pthread_mutex_lock(&mutex_lock);
    threadNumInputs = numIn;
    for (int i = 0; i < numIn; i++) {
        thread_ki_Input[i] = layerInputs[i];
    }
    for (int i = 0; i < neuronsInLayer; i++) {
        for (int j = 0; j < numIn; j++) {
            thread_ka_Weights[i][j] = layerWeights[i][j];
        }
        thread_ka_Index[i] = i;
    }
    pthread_mutex_unlock(&mutex_lock);

    // Create threads for each neuron
    for (int i = 0; i < neuronsInLayer; i++) {
        pthread_create(&threads[i], NULL, neuron_computation, &thread_ka_Index[i]);
    }

    // Wait for all threads to complete
    for (int i = 0; i < neuronsInLayer; i++) {
        pthread_join(threads[i], NULL);
    }

    // Copy outputs
    for (int i = 0; i < neuronsInLayer; i++) {
        layerResult[i] = threadOutputs[i];
    }

    // Print status
    cout << "Forward Pass " << passNum << " - Layer " << layerNum << " completed. Outputs: ";
    for (int i = 0; i < neuronsInLayer; i++) {
        cout << layerResult[i];
        if (i < neuronsInLayer - 1) cout << ", ";
    }
    cout << endl;
}

int main() {
    int hiddenLayerCount, neuronsPerLayer;

    // Get user input for layers and neurons
    cout << "Enter number of hidden layers: ";
    cin >> hiddenLayerCount;
    cout << "Enter number of neurons per hidden/output layer: ";
    cin >> neuronsPerLayer;

    // Synchronization primitives initialize kiye jate hain:
//  - mutex threads ke darmiyan shared data (inputs/weights/outputs)
//    par race condition rokne ke liye.
//  - semaphore critical section access ko control karne ke liye.
    pthread_mutex_init(&mutex_lock, NULL);
    sem_init(&sem, 0, 1);

    // Open input file
    //  - pehli line: initial inputs,
//  - baqi lines: har layer ke weights stored hain. Agar file na mile
//  to program error de kar band ho jata hai.
    ifstream inFile("input.txt");
    if (!inFile.is_open()) {
        cerr << "Error: Cannot open input.txt" << endl;
        return 1;
    }

    // Open output file
    outputFile.open("output.txt");
    if (!outputFile.is_open()) {
        cerr << "Error: Cannot open output.txt" << endl;
        return 1;
    }

    // Read initial inputs (for 2 input neurons)
    
// Pehli line se initial inputs read kiye jate hain (2 input neurons ke
// liye). readLine comma-separated doubles ko parse karke array me
// store karta hai
    double initialInputs[num_of_neurons];
    int numInitialInputs = readLine(inFile, initialInputs, num_of_neurons);

    cout << "\n=== Neural Network Simulation ===" << endl;
    cout << "Hidden Layers: " << hiddenLayerCount << endl;
    cout << "Neurons per layer: " << neuronsPerLayer << endl;
    cout << "Initial Inputs: " << initialInputs[0] << ", " << initialInputs[1] << endl;

    outputFile << "=== Neural Network Simulation ===" << endl;
    outputFile << "Hidden Layers: " << hiddenLayerCount << endl;
    outputFile << "Neurons per layer: " << neuronsPerLayer << endl;

    // Total layers: input + hidden + output
    int totalLayers = 1 + hiddenLayerCount + 1;

    // Create pipes for IPC between layers
    
    // Har adjacent layer pair ke beech forward data bhejne ke liye pipes[]
// create kiye jate hain (unnamed pipes, IPC between processes).
    int pipes[MAX_LAYERS][2];
    for (int i = 0; i < totalLayers - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            cerr << "Pipe creation failed" << endl;
            return 1;
        }
    }


    // Create pipes for backward pass
    
    // Backward pass ke liye alag backPipes[] banaye jate hain jinke zariye
// output layer se f(x1), f(x2) signals wapas input layer tak propagate
// hote hain. Ye ek simple feedback mechanism hai (
    int backPipes[MAX_LAYERS][2];
    for (int i = 0; i < totalLayers - 1; i++) {
        if (pipe(backPipes[i]) == -1) {
            cerr << "Backward pipe creation failed" << endl;
            return 1;
        }
    }

  
    
    // Saari layers ke weights ek 3D array networkWeights me load kiye jate hain:
// [layer][neuron][weightIndex]. is se har process apne layer ke weights
// easily access kar sakta hai
    double networkWeights[MAX_LAYERS][num_of_neurons][num_of_neurons];
    int weightsPerNeuron[MAX_LAYERS];




   // Input layer ke 2 neurons ke weights (har neuron ke liye
// neuronsPerLayer weights) file se readLine ke zariye read kiye jate
// hain.
    weightsPerNeuron[0] = neuronsPerLayer;
    for (int n = 0; n < 2; n++) {
        readLine(inFile, networkWeights[0][n], num_of_neurons);
    }

    // Hidden layers weights
    for (int layer = 0; layer < hiddenLayerCount; layer++) {
        weightsPerNeuron[layer + 1] = neuronsPerLayer;
        for (int n = 0; n < neuronsPerLayer; n++) {
            readLine(inFile, networkWeights[layer + 1][n], num_of_neurons);
        }
    }

    // Output layer weights
    weightsPerNeuron[hiddenLayerCount + 1] = neuronsPerLayer;
    for (int n = 0; n < neuronsPerLayer; n++) {
        readLine(inFile, networkWeights[hiddenLayerCount + 1][n], num_of_neurons);
    }

    inFile.close();

    // Har layer ke outputs temporarily store karne ke liye arrays, aur
// neuronsInLayer me har layer ki neurons count track ki jati hai.
    double layerResult[MAX_LAYERS][num_of_neurons];
    int neuronsInLayer[MAX_LAYERS];

    neuronsInLayer[0] = 2;  // Input layer has 2 neurons
    for (int i = 1; i <= hiddenLayerCount + 1; i++) {
        neuronsInLayer[i] = neuronsPerLayer;
    }

    // ==================== FIRST FORWARD PASS ====================
    
    // Pehla forward pass poori network par processes + threads ki madad se
// parallel taur par run hota hai: input → hidden(s) → output. Ye 
// feedforward computation hai.
    cout << "\n=== FIRST FORWARD PASS ===" << endl;
    outputFile << "\n=== FIRST FORWARD PASS ===" << endl;

    // Input Layer Process
    pid_t inputPid = fork();

    if (inputPid == 0) {
        // Child process - Input Layer
        cout << "\n[Input Layer Process Started - PID: " << getpid() << "]" << endl;

        pthread_t inputThreads[2];
        double inputLayerOutputs[num_of_neurons];

        
    // Input layer me har neuron apne input ko apne weights ke sath
    // multiply karke weighted sum nikalta hai. Yahan global thread
    // arrays bhi set kiye jate hain (agar threads use karne hon)
        pthread_mutex_lock(&mutex_lock);
        threadNumInputs = neuronsPerLayer;
        thread_ki_Input[0] = initialInputs[0];
        thread_ki_Input[1] = initialInputs[1];

        // Input neurons use weights differently - multiply input with each weight
        // Input neurons ke weights networkWeights se copy kiye jate hain.
        for (int n = 0; n < 2; n++) {
            thread_ka_Index[n] = n;
            for (int w = 0; w < neuronsPerLayer; w++) {
                thread_ka_Weights[n][w] = networkWeights[0][n][w];
            }
        }
        pthread_mutex_unlock(&mutex_lock);

        // Compute weighted outputs for input neurons
        
        
    // Yahan actual weighted sum sequentially nikala ja raha hai:
    // neuronSum = input * har weight ka sum.
        for (int n = 0; n < 2; n++) {
            double neuronSum = 0.0;
            for (int w = 0; w < neuronsPerLayer; w++) {
                neuronSum += initialInputs[n] * networkWeights[0][n][w];
            }
            inputLayerOutputs[n] = neuronSum;
        }

        cout << "Input Layer Output (Neuron 0): " << inputLayerOutputs[0] << endl;
        cout << "Input Layer Output (Neuron 1): " << inputLayerOutputs[1] << endl;

         // Ye outputs next layer ke process ko forward pipe ke zariye bheje
    // jate hain
        close(pipes[0][0]);  // Close read end
        write(pipes[0][1], inputLayerOutputs, sizeof(double) * 2);
        close(pipes[0][1]);

       
    // Backward pass ke dauran output layer se f(x1), f(x2) backPipe
    // par receive kiye jate hain taa-ke input layer bhi unhe print
    // kar sake.
        double backSignal[num_of_neurons];
        close(backPipes[0][1]);  // Close write end
        read(backPipes[0][0], backSignal, sizeof(double) * 2);
        close(backPipes[0][0]);

        cout << "\n[Input Layer received backward signal: f(x1)=" << backSignal[0] 
             << ", f(x2)=" << backSignal[1] << "]" << endl;

        exit(0);
    }

    // Hidden Layers Processes
    
// Hidden layers ke liye alag-alag child processes banaye jate hain jo
// apne inputs pipe se lete hain, threads se neuron outputs compute karte
// hain, aur agle layer ko outputs bhej dete hain.
    pid_t hiddenPids[MAX_LAYERS];

    for (int h = 0; h < hiddenLayerCount; h++) {
        hiddenPids[h] = fork();

        if (hiddenPids[h] == 0) {
            // Child process - Hidden Layer
            cout << "\n[Hidden Layer " << (h + 1) << " Process Started - PID: " << getpid() << "]" << endl;

            double hiddenInputs[num_of_neurons];
            double hiddenOutputs[num_of_neurons];
            int prevNeurons = (h == 0) ? 2 : neuronsPerLayer;

            // Read from previous layer
            close(pipes[h][1]);  // Close write end
            read(pipes[h][0], hiddenInputs, sizeof(double) * prevNeurons);
            close(pipes[h][0]);

            cout << "Hidden Layer " << (h + 1) << " received inputs from previous layer" << endl;

            // Create threads for each neuron
            pthread_t hiddenThreads[num_of_neurons];

            pthread_mutex_lock(&mutex_lock);
            threadNumInputs = prevNeurons;
            for (int i = 0; i < prevNeurons; i++) {
                thread_ki_Input[i] = hiddenInputs[i];
            }
            for (int n = 0; n < neuronsPerLayer; n++) {
                thread_ka_Index[n] = n;
                for (int w = 0; w < prevNeurons; w++) {
                    thread_ka_Weights[n][w] = networkWeights[h + 1][n][w];
                }
            }
            pthread_mutex_unlock(&mutex_lock);

            // Create threads
            for (int n = 0; n < neuronsPerLayer; n++) {
                pthread_create(&hiddenThreads[n], NULL, neuron_computation, &thread_ka_Index[n]);
            }

            // Wait for threads
            for (int n = 0; n < neuronsPerLayer; n++) {
                pthread_join(hiddenThreads[n], NULL);
            }

            // Copy outputs
            for (int n = 0; n < neuronsPerLayer; n++) {
                hiddenOutputs[n] = threadOutputs[n];
            }

            cout << "Hidden Layer " << (h + 1) << " Outputs: ";
            for (int n = 0; n < neuronsPerLayer; n++) {
                cout << hiddenOutputs[n];
                if (n < neuronsPerLayer - 1) cout << ", ";
            }
            cout << endl;

            // Send to next layer
            close(pipes[h + 1][0]);  // Close read end
            write(pipes[h + 1][1], hiddenOutputs, sizeof(double) * neuronsPerLayer);
            close(pipes[h + 1][1]);

            // Phir ye signal pichle layer ko backPipes[h] se send kar diya
        // jata hai, is tarah backward chain complete hoti hai.
       
            double backSignal[num_of_neurons];
            close(backPipes[h + 1][1]);
            read(backPipes[h + 1][0], backSignal, sizeof(double) * 2);
            close(backPipes[h + 1][0]);

            cout << "Hidden Layer " << (h + 1) << " received backward signal" << endl;

            // Pass backward
            close(backPipes[h][0]);
            write(backPipes[h][1], backSignal, sizeof(double) * 2);
            close(backPipes[h][1]);

            exit(0);
        }
    }

   // Output layer ka process, jo last hidden layer se inputs le kar threads
// se neuron outputs compute karta hai, total sum nikalta hai, aur
// formulas se f(x1), f(x2) calculate karta hai.
    pid_t outputPid = fork();

    if (outputPid == 0) {
        // Child process - Output Layer
        cout << "\n[Output Layer Process Started - PID: " << getpid() << "]" << endl;

        double outputInputs[num_of_neurons];
        double outputLayerOutputs[num_of_neurons];
        int lastPipeIdx = hiddenLayerCount;

        // Read from last hidden layer
        close(pipes[lastPipeIdx][1]);
        read(pipes[lastPipeIdx][0], outputInputs, sizeof(double) * neuronsPerLayer);
        close(pipes[lastPipeIdx][0]);

        cout << "Output Layer received inputs from hidden layer" << endl;

        // Create threads for output neurons
        pthread_t outputThreads[num_of_neurons];

        pthread_mutex_lock(&mutex_lock);
        threadNumInputs = neuronsPerLayer;
        for (int i = 0; i < neuronsPerLayer; i++) {
            thread_ki_Input[i] = outputInputs[i];
        }
        for (int n = 0; n < neuronsPerLayer; n++) {
            thread_ka_Index[n] = n;
            for (int w = 0; w < neuronsPerLayer; w++) {
                thread_ka_Weights[n][w] = networkWeights[hiddenLayerCount + 1][n][w];
            }
        }
        pthread_mutex_unlock(&mutex_lock);

        // Create threads
        for (int n = 0; n < neuronsPerLayer; n++) {
            pthread_create(&outputThreads[n], NULL, neuron_computation, &thread_ka_Index[n]);
        }

        // Wait for threads
        for (int n = 0; n < neuronsPerLayer; n++) {
            pthread_join(outputThreads[n], NULL);
        }

         // Neuron outputs se ek totalOutput sum banaya jata hai jo aage
    // f(x1), f(x2) ke formula me use hota hai.
        double totalOutput = 0.0;
        for (int n = 0; n < neuronsPerLayer; n++) {
            outputLayerOutputs[n] = threadOutputs[n];
            totalOutput += outputLayerOutputs[n];
        }

        cout << "Output Layer neuron outputs: ";
        for (int n = 0; n < neuronsPerLayer; n++) {
            cout << outputLayerOutputs[n];
            if (n < neuronsPerLayer - 1) cout << ", ";
        }
        cout << endl;
        cout << "Total Output Sum: " << totalOutput << endl;

     // yahan pe project ke given mathematical formulas apply kiye jate
    // hain:
    // f(x1) = (output^2 + output + 1)/2
    // f(x2) = (output^2 - output)/2
    // ye ek artificial mapping hai jo final scalar output ko do
    // functions me convert karti hai
        double fx1 = (totalOutput * totalOutput + totalOutput + 1) / 2.0;
        double fx2 = (totalOutput * totalOutput - totalOutput) / 2.0;

        cout << "\n=== BACKWARD PASS ===" << endl;
        cout << "f(x1) = (output^2 + output + 1) / 2 = " << fx1 << endl;
        cout << "f(x2) = (output^2 - output) / 2 = " << fx2 << endl;

    // Ye dono values backPipes ke through wapas
    // hidden + input layers ko bheji jati hain, sirf demonstration ke
    // liye (yahan gradients/weights update nahi hote, sirf values
    // propagate ho rahi hain).
    
        double backSignal[2] = {fx1, fx2};
        close(backPipes[lastPipeIdx][0]);
        write(backPipes[lastPipeIdx][1], backSignal, sizeof(double) * 2);
        close(backPipes[lastPipeIdx][1]);

        cout << "Output Layer sent backward signal to previous layers" << endl;

        // Write first forward pass results to file
        ofstream outFile("output.txt", ios::app);
        outFile << "\n=== FIRST FORWARD PASS RESULTS ===" << endl;
        outFile << "Total Output: " << totalOutput << endl;
        outFile << "f(x1) = " << fx1 << endl;
        outFile << "f(x2) = " << fx2 << endl;
        outFile.close();

        exit(0);
    }

    // Parent waits for all children
    waitpid(inputPid, NULL, 0);
    for (int h = 0; h < hiddenLayerCount; h++) {
        waitpid(hiddenPids[h], NULL, 0);
    }
    waitpid(outputPid, NULL, 0);

    // Close remaining pipe ends in parent
    for (int i = 0; i < totalLayers - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
        close(backPipes[i][0]);
        close(backPipes[i][1]);
    }

    cout << "\n=== SECOND FORWARD PASS (using f(x1) and f(x2) as new inputs) ===" << endl;

    // Re-read input file for second forward pass
    ifstream inFile2("input.txt");
    double tempInputs[num_of_neurons];
    readLine(inFile2, tempInputs, num_of_neurons);  // Skip first line

    // Re-read weights
    for (int n = 0; n < 2; n++) {
        readLine(inFile2, networkWeights[0][n], num_of_neurons);
    }
    for (int layer = 0; layer < hiddenLayerCount; layer++) {
        for (int n = 0; n < neuronsPerLayer; n++) {
            readLine(inFile2, networkWeights[layer + 1][n], num_of_neurons);
        }
    }
    for (int n = 0; n < neuronsPerLayer; n++) {
        readLine(inFile2, networkWeights[hiddenLayerCount + 1][n], num_of_neurons);
    }
    inFile2.close();

   
// Second pass ke liye naye pipes banaye jate hain, lekin computation
// ab sequential (single process) rakhi gayi hai, multithreading/process
// nahi use ho raha; ye sirf mathematical re-run hai.

    int pipes2[MAX_LAYERS][2];
    for (int i = 0; i < totalLayers - 1; i++) {
        pipe(pipes2[i]);
    }

    // output.txt se f(x1) aur f(x2) ki last values parse karke
// secondInputs me store ki jati hain. Ye pehle pass ke result ko
// dusre pass ke inputs banata hai, jise aap ek simple feedback
// experiment samajh sakte hain.
    double fx1_new, fx2_new;
    double secondInputs[2];

    ifstream readOut("output.txt");
    string line;
    while (getline(readOut, line)) {
        if (line.find("f(x1)") != string::npos && line.find("=") != string::npos) {
            size_t pos = line.rfind("=");
            if (pos != string::npos) {
                fx1_new = atof(line.substr(pos + 1).c_str());
            }
        }
        if (line.find("f(x2)") != string::npos && line.find("=") != string::npos) {
            size_t pos = line.rfind("=");
            if (pos != string::npos) {
                fx2_new = atof(line.substr(pos + 1).c_str());
            }
        }
    }
    readOut.close();

    secondInputs[0] = fx1_new;
    secondInputs[1] = fx2_new;

    cout << "New Inputs for Second Pass: " << secondInputs[0] << ", " << secondInputs[1] << endl;

    // Second Forward Pass - Sequential computation  (no fork, no pthreads)
    double currentOutputs[num_of_neurons];
    double nextOutputs[num_of_neurons];
    int currentCount = 2;

    currentOutputs[0] = secondInputs[0];
    currentOutputs[1] = secondInputs[1];

    // Input layer computation
    cout << "\n[Second Pass - Input Layer]" << endl;
    double inputOut[2] = {0.0, 0.0};
    for (int n = 0; n < 2; n++) {
        for (int w = 0; w < neuronsPerLayer; w++) {
            inputOut[n] += secondInputs[n] * networkWeights[0][n][w];
        }
    }
    cout << "Input Layer Outputs: " << inputOut[0] << ", " << inputOut[1] << endl;

    currentOutputs[0] = inputOut[0];
    currentOutputs[1] = inputOut[1];
    currentCount = 2;

    // Hidden layers
    for (int h = 0; h < hiddenLayerCount; h++) {
        cout << "\n[Second Pass - Hidden Layer " << (h + 1) << "]" << endl;

        for (int n = 0; n < neuronsPerLayer; n++) {
            nextOutputs[n] = 0.0;
            for (int i = 0; i < currentCount; i++) {
                nextOutputs[n] += currentOutputs[i] * networkWeights[h + 1][n][i];
            }
        }

        cout << "Hidden Layer " << (h + 1) << " Outputs: ";
        for (int n = 0; n < neuronsPerLayer; n++) {
            currentOutputs[n] = nextOutputs[n];
            cout << currentOutputs[n];
            if (n < neuronsPerLayer - 1) cout << ", ";
        }
        cout << endl;
        currentCount = neuronsPerLayer;
    }

    // Output layer
    cout << "\n[Second Pass - Output Layer]" << endl;
    double finalSum = 0.0;
    for (int n = 0; n < neuronsPerLayer; n++) {
        nextOutputs[n] = 0.0;
        for (int i = 0; i < neuronsPerLayer; i++) {
            nextOutputs[n] += currentOutputs[i] * networkWeights[hiddenLayerCount + 1][n][i];
        }
        finalSum += nextOutputs[n];
    }

    cout << "Output Layer neuron outputs: ";
    for (int n = 0; n < neuronsPerLayer; n++) {
        cout << nextOutputs[n];
        if (n < neuronsPerLayer - 1) cout << ", ";
    }
    cout << endl;
    cout << "Final Output Sum: " << finalSum << endl;

    
// Second pass ke final scalar output par bhi same formulas apply
// kiye jate hain taa-ke aap dekh saken ke feedback ke baad values kitni
// grow karti hain. 
    double final_fx1 = (finalSum * finalSum + finalSum + 1) / 2.0;
    double final_fx2 = (finalSum * finalSum - finalSum) / 2.0;

    cout << "\nFinal f(x1) = " << final_fx1 << endl;
    cout << "Final f(x2) = " << final_fx2 << endl;

    // Write second forward pass results
    ofstream outFile2("output.txt", ios::app);
    outFile2 << "\n=== SECOND FORWARD PASS RESULTS ===" << endl;
    outFile2 << "New Inputs: " << secondInputs[0] << ", " << secondInputs[1] << endl;
    outFile2 << "Final Output Sum: " << finalSum << endl;
    outFile2 << "Final f(x1) = " << final_fx1 << endl;
    outFile2 << "Final f(x2) = " << final_fx2 << endl;
    outFile2.close();

    // Cleanup
    pthread_mutex_destroy(&mutex_lock);
    sem_destroy(&sem);
    for (int i = 0; i < totalLayers - 1; i++) {
        close(pipes2[i][0]);
        close(pipes2[i][1]);
    }

    cout << "\n=== Simulation Complete ===" << endl;
    cout << "Results written to output.txt" << endl;

    outputFile.close();

    return 0;
}

