//WORK DONE BY XUDONG CHEN
#include<iostream>
#include<vector>
#include<string>
#include<fstream>
#include<thread>
#include<mutex>
#include<condition_variable>
#include<chrono>

using namespace std;
using namespace chrono;
using namespace literals::chrono_literals;

const int MaxTimePart{ 30000 }, MaxTimeProduct{ 28000 };

mutex coutM;
mutex bufferM;
condition_variable cv1, cv2;
ofstream Out;
int totalComplete;
int partWorkerWaitingAver = 0;

class Part {
public:
    int prodTime;
    int moveTime;
    int assembleTime;
    Part(int prodTime, int moveTime, int assembleTime) {
        this->prodTime = prodTime;
        this->moveTime = moveTime;
        this->assembleTime = assembleTime;
    }
};
class Buffer {
public:
    const vector<int> bufferCapacity = { 7,6,5,5,4, 7,6,5,5,4 };
    vector<int> bufferState;
    int totalComplete;
    Buffer() {
        bufferState = { 0, 0, 0, 0, 0, 0,0,0,0,0 };
        totalComplete = 0;
    }
    bool isFirstBufferFull(vector<int> buf) {
        for (int i = 0; i < 5; i++) {
            if (buf[i] < bufferCapacity[i]) {
                return false;
            }
        }
        return true;
    }
    bool isFirstBufferEmpty(vector<int> buf) {
        for (int i = 0; i < 5; i++) {
            if (buf[i] != 0) {
                return false;
            }
        }
        return true;
    }
    int size() {
        return bufferCapacity.size();
    }
};
class PartWorker {
public:
    int id;
    int loadOrderSize;
    microseconds maxWaitTime;
    vector<int> loadOrder;
    vector<int> partLocalState;

    PartWorker(int id) {
        this->id = id;
        this->maxWaitTime = (microseconds)MaxTimePart;
        int size = 5;
        loadOrderSize = 6;
        loadOrder = vector<int>(size, 0);
        partLocalState = vector<int>(size, 0);
        int count = 6;
        while (count > 0) {
            loadOrder[rand() % size]++;
            count--;
        }
    }

    void partWorkerRun(Buffer& buffer, const vector<Part>& parts, int iter, system_clock::time_point& systemStartTime) {
        vector<string> status{ "New Load Order", "Wakeup-Notified", " Wakeup-Timeout" };
        int waitTime = 0;
        for (int i = 0; i < loadOrder.size(); i++) {
            waitTime += (loadOrder[i] - partLocalState[i]) * parts[i].prodTime;
        }
        if (waitTime != 0) {
            this_thread::sleep_for(microseconds(waitTime));
        }

        waitTime = 0;

        int numPartToDrop = 0;
        partLocalState = loadOrder;
        for (int partCount : partLocalState) {
            numPartToDrop += partCount;
        }
        //put into buffer, a lock for buffer
        system_clock::duration waitLength = (microseconds)maxWaitTime;
        system_clock::time_point startTime;
        system_clock::duration dur((microseconds)0);
        vector<int> prevBufferState, prevLocalState, currBufferState;

        int statusIndex = 0;
        unique_lock<mutex> UG1(bufferM);
        bool bufferFirstFull = false;
        while (1) {
            startTime = system_clock::now();
            prevBufferState = buffer.bufferState;
            //Out << "***********************" << buffer.isFirstBufferFull(prevBufferState) << "******************************" << endl;
            bufferFirstFull = buffer.isFirstBufferFull(prevBufferState);
            if (bufferFirstFull) {
                //load to the second part of buffer(i : 5 -> 9)
                prevLocalState = partLocalState; // load order
                int prevNumPartToDrop = numPartToDrop;
                for (int i = 0; i < partLocalState.size(); i++) {
                    int dropCount = buffer.bufferCapacity[i + 5] - buffer.bufferState[i + 5];
                    int numDroppedPart = dropCount > partLocalState[i] ? partLocalState[i] : dropCount;
                    buffer.bufferState[i + 5] += numDroppedPart;
                    partLocalState[i] -= numDroppedPart;
                    numPartToDrop -= numDroppedPart;
                }

                currBufferState = buffer.bufferState;
                // drop some, do some printing
                if (prevNumPartToDrop != numPartToDrop || statusIndex == 0) {
                    unique_lock<mutex> LG(coutM);
                    Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                    Out << "Iteration " << iter << endl;
                    Out << "Part Worker ID: " << this->id << endl;
                    Out << "Status: " << status[statusIndex] << endl;
                    Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                    Out << "Buffer State: ("; for (int item : prevBufferState) Out << item << ","; Out << ")" << endl;

                    Out << "Load Order: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Load Order: ("; for (int item : partLocalState) Out << item << ","; Out << ")" << endl;
                    Out << endl;
                }

                statusIndex = 1;
                if (numPartToDrop == 0) {
                    break;
                }

                prevBufferState = buffer.bufferState;
                prevLocalState = partLocalState;
                system_clock::time_point startWaitPoint = system_clock::now();

                if (cv1.wait_for(UG1, waitLength) == cv_status::timeout) {
                    dur = dur + (system_clock::now() - startWaitPoint);

                    waitTime = 0;
                    if (numPartToDrop != 0) {
                        for (int i = 0; i < partLocalState.size(); i++)
                            waitTime += partLocalState[i] * parts[i].moveTime;

                        if (waitTime != 0)
                            this_thread::sleep_for(microseconds(waitTime));
                    }

                    {
                        statusIndex = 2;
                        lock_guard<mutex> LG(coutM);
                        Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                        Out << "Iteration " << iter << endl;
                        Out << "Part Worker ID: " << this->id << endl;
                        Out << "Status: " << status[statusIndex] << endl;
                        Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                        Out << "Buffer State: ("; for (int item : prevBufferState)  Out << item << ",";  Out << ")" << endl;
                        Out << "Load Order: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                        Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                        Out << "Updated Load Order: ("; for (int item : partLocalState) Out << item << ","; Out << ")" << endl;
                        Out << endl;
                    }
                    break;
                }
                partWorkerWaitingAver += duration_cast<microseconds>(dur).count();
                system_clock::duration dur2 = system_clock::now() - startWaitPoint;
                dur += dur2;
                waitLength -= dur2;
            }
            else {
                //load to the first part of buffer
                prevLocalState = partLocalState; // load order
                int prevNumPartToDrop = numPartToDrop;
                for (int i = 0; i < partLocalState.size(); i++) {
                    int dropCount = buffer.bufferCapacity[i] - buffer.bufferState[i];
                    int numDroppedPart = dropCount > partLocalState[i] ? partLocalState[i] : dropCount;
                    buffer.bufferState[i] += numDroppedPart;
                    partLocalState[i] -= numDroppedPart;
                    numPartToDrop -= numDroppedPart;
                }

                currBufferState = buffer.bufferState;
                // drop some, do some printing
                if (prevNumPartToDrop != numPartToDrop || statusIndex == 0) {
                    unique_lock<mutex> LG(coutM);
                    Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                    Out << "Iteration " << iter << endl;
                    Out << "Part Worker ID: " << this->id << endl;
                    Out << "Status: " << status[statusIndex] << endl;
                    Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                    Out << "Buffer State: ("; for (int item : prevBufferState) Out << item << ","; Out << ")" << endl;

                    Out << "Load Order: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Load Order: ("; for (int item : partLocalState) Out << item << ","; Out << ")" << endl;
                    Out << endl;
                }

                statusIndex = 1;
                if (numPartToDrop == 0) {
                    break;
                }

                prevBufferState = buffer.bufferState;
                prevLocalState = partLocalState;
                system_clock::time_point startWaitPoint = system_clock::now();

                if (cv1.wait_for(UG1, waitLength) == cv_status::timeout) {
                    dur = dur + (system_clock::now() - startWaitPoint);

                    waitTime = 0;
                    if (numPartToDrop != 0) {
                        for (int i = 0; i < partLocalState.size(); i++)
                            waitTime += partLocalState[i] * parts[i].moveTime;

                        if (waitTime != 0)
                            this_thread::sleep_for(microseconds(waitTime));
                    }

                    {
                        statusIndex = 2;
                        lock_guard<mutex> LG(coutM);
                        Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                        Out << "Iteration " << iter << endl;
                        Out << "Part Worker ID: " << this->id << endl;
                        Out << "Status: " << status[statusIndex] << endl;
                        Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                        Out << "Buffer State: ("; for (int item : prevBufferState)  Out << item << ",";  Out << ")" << endl;
                        Out << "Load Order: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                        Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                        Out << "Updated Load Order: ("; for (int item : partLocalState) Out << item << ","; Out << ")" << endl;
                        Out << endl;
                    }
                    break;
                }

                system_clock::duration dur2 = system_clock::now() - startWaitPoint;
                partWorkerWaitingAver += duration_cast<microseconds>(dur).count();
                dur += dur2;
                waitLength -= dur2;


                //check again if the first part is full, if it is not null, move the second part to the first part
                bool full = buffer.isFirstBufferFull(prevBufferState);
                if (!full) {
                    //move the second part to the first part
                    for (int i = 0; i < 5; i++) {
                        int moveCount = buffer.bufferCapacity[i] - buffer.bufferState[i];
                        int numMovePart = moveCount > buffer.bufferState[i + 5] ? buffer.bufferState[i + 5] : moveCount;
                        buffer.bufferState[i] += numMovePart;
                        buffer.bufferState[i + 5] -= numMovePart;
                    }
                }
            }


            //Out << "***********************" << buffer.isFirstBufferFull(prevBufferState) << "******************************" << endl;
        }

        cv2.notify_all();

    }
};

class ProductWorker {
public:
    int id;
    int assembleOrderSize;
    int partTypes;
    int size;
    microseconds maxWaitTime;
    vector<int> assembleOrder;
    vector<int> cartOrder;

    ProductWorker(int id) {
        this->id = id;
        this->maxWaitTime = (microseconds)MaxTimeProduct;
        partTypes = 5;
        size = 5;
        assembleOrderSize = 5;
        assembleOrder = vector<int>(size, 0);
        cartOrder = vector<int>(size, 0);
        assembleOrder = cartOrder;

        int numTypes = 0;
        int count = 0;
        int bitCount = 0;
        vector<int> flag = vector<int>(size, false);
        while (true) {
            int i = rand() % size;
            if (flag[i] == false && bitCount < 3) {
                assembleOrder[i]++;
                flag[i] = true;
                bitCount++;
                count++;
            }
            else if (flag[i] == true) {
                assembleOrder[i]++;
                count++;
            }

            if (count == 5) {
                break;
            }
        }
    }

    void productWorkerRun(Buffer& buffer, const vector<Part>& parts, int iteration, system_clock::time_point& systemStartTime) {
        int waitTime = 0;
        vector<string> status{ "New Pickup Order", "Wakeup-Notified", " Wakeup-Timeout" };
        int numPartToPickUp = 0;
        vector<int> localState = cartOrder, pickUpOrder = vector<int>(size, 0), realCartState = vector<int>(size, 0);
        for (int i = 0; i < cartOrder.size(); i++) {
            pickUpOrder[i] = assembleOrder[i] - cartOrder[i];
            numPartToPickUp += assembleOrder[i] - cartOrder[i];
        }
        system_clock::duration waitLength = (microseconds)maxWaitTime;
        system_clock::time_point startTime = system_clock::now();
        system_clock::duration dur(0);
        vector<int> prevBufferState, prevCartState, prevPickUpOrder, prevLocalState, currBufferState;
        int statusIndex = 0;

        unique_lock<mutex> UG2(bufferM);
        while (1) {
            //a lock here preventing buffer be accessed by partworker and productworker at the same time
            startTime = system_clock::now();

            prevBufferState = buffer.bufferState;
            prevCartState = realCartState;
            prevPickUpOrder = pickUpOrder;
            prevLocalState = localState;
            int prevNumPartToPickUp = numPartToPickUp;

            //step1:
            for (int i = 0; i < buffer.size() / 2; i++) {
                int picked = assembleOrder[i] - cartOrder[i];
                int numPickedUpPart = buffer.bufferState[i] > picked ? picked : buffer.bufferState[i];

                buffer.bufferState[i] -= numPickedUpPart;
                cartOrder[i] += numPickedUpPart;
                realCartState[i] += numPickedUpPart;
                numPartToPickUp -= numPickedUpPart;
                pickUpOrder[i] -= numPickedUpPart;
            }

            currBufferState = buffer.bufferState;

            if (numPartToPickUp == 0) {
                totalComplete++;
                waitTime = 0;
                for (int i = 0; i < cartOrder.size(); i++) {
                    waitTime += cartOrder[i] * parts[i].assembleTime;
                }

                if (waitTime != 0) {
                    this_thread::sleep_for(microseconds(waitTime));
                }

                {
                    lock_guard<mutex> LG(coutM);
                    Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                    Out << "Iteration " << iteration << endl;
                    Out << "Product Worker ID: " << this->id << endl;
                    Out << "Status: " << status[statusIndex] << endl;
                    Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                    Out << "Buffer State: ("; for (int item : prevBufferState) Out << item << ","; Out << ")" << endl;
                    Out << "Pickup Order: ("; for (int item : prevPickUpOrder) Out << item << ","; Out << ")" << endl;
                    Out << "Local State: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                    Out << "Cart State: ("; for (int item : prevCartState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Pickup Order: ("; for (int item : pickUpOrder) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Local State: ("; for (int item : localState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Cart State: ("; for (int item : realCartState) Out << item << ","; Out << ")" << endl;
                    vector<int> temp(pickUpOrder.size(), 0);
                    Out << "Updated Local State: ("; for (int item : temp) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Cart State: ("; for (int item : temp) Out << item << ","; Out << ")" << endl;
                    Out << "Total Completed Products: " << totalComplete << endl;
                    Out << endl;


                }
                cartOrder = vector<int>(partTypes, 0);
                realCartState = vector<int>(partTypes, 0);
                localState = vector<int>(partTypes, 0);
                break;
            }

            if (prevNumPartToPickUp != numPartToPickUp || statusIndex == 0) {
                lock_guard<mutex> LG(coutM);
                Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                Out << "Iteration " << iteration << endl;
                Out << "Product Worker ID: " << this->id << endl;
                Out << "Status: " << status[statusIndex] << endl;
                Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                Out << "Buffer State: ("; for (int item : prevBufferState) Out << item << ","; Out << ")" << endl;
                Out << "Pickup Order: ("; for (int item : prevPickUpOrder) Out << item << ","; Out << ")" << endl;
                Out << "Local State: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                Out << "Cart State: ("; for (int item : prevCartState) Out << item << ","; Out << ")" << endl;
                Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                Out << "Updated Pickup Order: ("; for (int item : pickUpOrder) Out << item << ","; Out << ")" << endl;
                Out << "Updated Local State: ("; for (int item : localState) Out << item << ","; Out << ")" << endl;
                Out << "Updated Cart State: ("; for (int item : realCartState) Out << item << ","; Out << ")" << endl;
                Out << "Total Completed Products: " << totalComplete << endl;
                Out << endl;
            }
            statusIndex = 1;

            prevBufferState = buffer.bufferState;
            prevCartState = realCartState;
            prevPickUpOrder = pickUpOrder;
            prevLocalState = localState;
            system_clock::time_point startWaitPoint = system_clock::now();

            if (cv1.wait_for(UG2, waitLength) == cv_status::timeout) {
                dur += (system_clock::now() - startWaitPoint);
                waitTime = 0;
                for (int i = 0; i < cartOrder.size(); i++) {
                    waitTime += (cartOrder[i] - localState[i]) * parts[i].moveTime;
                }

                if (waitTime != 0) {
                    this_thread::sleep_for(microseconds(waitTime));
                }
                {
                    lock_guard<mutex> LG(coutM);
                    statusIndex = 2;
                    Out << "Current Time: " << (startTime.time_since_epoch().count() - systemStartTime.time_since_epoch().count()) << "us" << endl;
                    Out << "Iteration " << iteration << endl;
                    Out << "Product Worker ID: " << this->id << endl;
                    Out << "Status: " << status[statusIndex] << endl;
                    Out << "Accumulated Wait Time: " << duration_cast<microseconds>(dur).count() << "us" << endl;
                    Out << "Buffer State: ("; for (int item : prevBufferState) Out << item << ","; Out << ")" << endl;
                    Out << "Pickup Order: ("; for (int item : prevPickUpOrder) Out << item << ","; Out << ")" << endl;
                    Out << "Local State: ("; for (int item : prevLocalState) Out << item << ","; Out << ")" << endl;
                    Out << "Cart State: ("; for (int item : prevCartState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Buffer State: ("; for (int item : currBufferState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Pickup Order: ("; for (int item : pickUpOrder) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Local State: ("; for (int item : localState) Out << item << ","; Out << ")" << endl;
                    Out << "Updated Cart State: ("; for (int item : realCartState) Out << item << ","; Out << ")" << endl;
                    Out << "Total Completed Products: " << totalComplete << endl;
                    Out << endl;
                }
                break;
            }
            system_clock::duration dur2 = system_clock::now() - startWaitPoint;
            dur += dur2;
            waitLength -= dur2;
        }
        cv1.notify_all();
        return;
    }
};

void partWorkerFactory(PartWorker& worker, Buffer& buffer, vector<Part>& parts, int iterationCount, system_clock::time_point& systemStartTime) {
    for (int i = 0; i < iterationCount; i++) {
        worker.partWorkerRun(buffer, parts, i + 1, systemStartTime);
    }
}

void prodWorkerFactory(ProductWorker& worker, Buffer& buffer, vector<Part>& parts, int iterationCount, system_clock::time_point& systemStartTime) {
    for (int i = 0; i < iterationCount; i++) {
        worker.productWorkerRun(buffer, parts, i + 1, systemStartTime);
    }
}

int main() {
    srand(time(NULL));
    system_clock::time_point startTime = system_clock::now();
    const int m = 20, n = 16;
    //m: number of Part Workers
    //n: number of Product Workers
    Out = ofstream("log.txt");
    totalComplete = 0;

    vector<Part> parts{ Part(500, 200, 600), Part(500, 200, 600), Part(600, 300, 700), Part(600, 300, 700), Part(700, 400, 800) };
    Buffer mainBuffer;
    vector<PartWorker> partWorkers;
    vector<ProductWorker> productWorkers;

    for (int i = 1; i <= m; i++) {
        partWorkers.push_back(PartWorker(i));
    }
    for (int i = 1; i <= n; i++) {
        productWorkers.push_back(ProductWorker(i));
    }

    int iterationCount = 5;
    vector<thread> partWorkerThreads;
    vector<thread> productWorkerThreads;
    for (int i = 0; i < m; i++) {
        partWorkerThreads.emplace_back(partWorkerFactory, ref(partWorkers[i]), ref(mainBuffer), ref(parts), iterationCount, ref(startTime));
    }
    this_thread::sleep_for(microseconds(50));
    for (int i = 0; i < n; i++) {
        productWorkerThreads.emplace_back(prodWorkerFactory, ref(productWorkers[i]), ref(mainBuffer), ref(parts), iterationCount, ref(startTime));
    }

    for (thread& t : partWorkerThreads) {
        t.join();
    }

    for (thread& t : productWorkerThreads) {
        t.join();
    }

    Out << "All work is Done!!!" << endl;
    cout << partWorkerWaitingAver / m << endl;;
    cout << "All work is Done!!!" << endl;
    Out.close();
    return 0;
}






