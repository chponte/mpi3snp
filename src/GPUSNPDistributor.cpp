/*
 * GPUSNPDistributor.cpp
 *
 *  Created on: Sep 30, 2014
 *      Author: gonzales
 */

#include "GPUSNPDistributor.h"

GPUSNPDistributor::GPUSNPDistributor(Options* options) {
	_options = options;
	_snpSet.resize(DEFAULT_NUM_SNPS);

	_withLock = (_options->getNumGPUs()) > 1;
	pthread_mutex_init(&_mutex, NULL);

	if((_fpOut = myfopen(options->getOutFileName().c_str(), "wb")) == NULL){
		Utils::exit("Out file: file %s could not be opened\n", options->getOutFileName().c_str());
	}

	if((_fpTfam = myfopen(options->getTFAMFileName().c_str(), "r")) == NULL){
		Utils::exit("TFAM file: file %s could not be opened\n", options->getTFAMFileName().c_str());
	}

	if((_fpTped = myfopen(options->getTPEDFileName().c_str(), "r")) == NULL){
		Utils::exit("TPED file: file %s could not be opened\n", options->getTPEDFileName().c_str());
	}

	_indsClass = new bool[DEFAULT_NUM_INDS];
	_boolArrSize = DEFAULT_NUM_INDS;
	_lineReader = new LineReader();

	_numSnp = 0;
	_numCases = 0;
	_numCtrls = 0;
	_numEntriesCase = 0;
	_numEntriesCtrl = 0;

	_moreDouble = true;

	_iterDoubleSnp1 = 0;
	_iterDoubleSnp2 = 1;

	if(_options->getNumGPUs()>0){
		_sizeArrCase = DEFAULT_NUM_INDS*DEFAULT_NUM_SNPS/32;
		_sizeArrCtrl = DEFAULT_NUM_INDS*DEFAULT_NUM_SNPS/32;

		cudaMallocHost(&_host0Cases, _sizeArrCase*sizeof(uint32_t));
		myCheckCudaError;
		cudaMallocHost(&_host0Ctrls, _sizeArrCtrl*sizeof(uint32_t));
		myCheckCudaError;
		cudaMallocHost(&_host1Cases, _sizeArrCase*sizeof(uint32_t));
		myCheckCudaError;
		cudaMallocHost(&_host1Ctrls, _sizeArrCtrl*sizeof(uint32_t));
		myCheckCudaError;
		cudaMallocHost(&_host2Cases, _sizeArrCase*sizeof(uint32_t));
		myCheckCudaError;
		cudaMallocHost(&_host2Ctrls, _sizeArrCtrl*sizeof(uint32_t));
		myCheckCudaError;
	}
}

GPUSNPDistributor::~GPUSNPDistributor() {

	myfclose(_fpTfam);
	myfclose(_fpTped);
	myfclose(_fpOut);

	delete _lineReader;

	if(_host0Cases){
		cudaFreeHost(_host0Cases);
		myCheckCudaError;
	}
	if(_host1Cases){
		cudaFreeHost(_host1Cases);
		myCheckCudaError;
	}
	if(_host2Cases){
		cudaFreeHost(_host2Cases);
		myCheckCudaError;
	}
	if(_host0Ctrls){
		cudaFreeHost(_host0Ctrls);
		myCheckCudaError;
	}
	if(_host1Ctrls){
		cudaFreeHost(_host1Ctrls);
		myCheckCudaError;
	}
	if(_host2Ctrls){
		cudaFreeHost(_host2Ctrls);
		myCheckCudaError;
	}
}

void GPUSNPDistributor::_loadIndsClass(){
	// Load the information from a TFAM file about the cases and controls
	int numInds = 0;
	int retValue;
	_numCases = 0;
	_numCtrls = 0;

	while((retValue = _lineReader->readTFAMLine(_fpTfam, numInds)) >= 0){
		if(numInds >= _boolArrSize){
			_resizeBoolArr(_boolArrSize);
		}
		if(retValue){
			_numCtrls++;
			_indsClass[numInds] = true;
		}
		else{
			_numCases++;
			_indsClass[numInds] = false;
		}
		numInds++;
	}

	Utils::log("Loaded information of %ld individuals (%ld/%ld cases/controls)\n", numInds, _numCases, _numCtrls);
#ifdef DEBUG
	for(int i=0; i<numInds; i++){
		if(_indsClass[i]){
			Utils::log("control ");
		}
		else{
			Utils::log("case ");
		}
	}
	Utils::log("\n");
#endif
}

void GPUSNPDistributor::loadSNPSet(){

	// Load the information of which individuals are cases and controls
	_loadIndsClass();

	_numEntriesCase = _numCases/32 + ((_numCases%32)>0);
	_numEntriesCtrl = _numCtrls/32 + ((_numCtrls%32)>0);

	SNP* readSNP;

	_numSnp = 0;
	uint32_t numFilteredSNP = 0;
	while(1){
		readSNP = new(SNP);

		if(!_lineReader->readTPEDLine(_fpTped, readSNP, _numSnp, _numCases+_numCtrls, _indsClass)){
			delete readSNP;
			break;
		}

		if(_snpSet.size() <= _numSnp){
			_snpSet.resize(_snpSet.size()+DEFAULT_NUM_SNPS);
		}
		_snpSet[_numSnp] = readSNP;

#ifdef DEBUG
			int j;
			Utils::log("SNP %d:\n   name %s\n   cases ", _numSnp, (char *)readSNP->_name);
			for(j=0; j<(_numCases/32+((_numCases%32)>0)); j++){
				Utils::log("%u ", readSNP->_case0Values[j]);
			}
			Utils::log("\n   cases 1 ");
			for(j=0; j<(_numCases/32+((_numCases%32)>0)); j++){
				Utils::log("%u ", readSNP->_case1Values[j]);
			}
			Utils::log("\n   cases 2 ");
			for(j=0; j<(_numCases/32+((_numCases%32)>0)); j++){
				Utils::log("%u ", readSNP->_case2Values[j]);
			}
			Utils::log("\n   controls 0 ");
			for(j=0; j<(_numCtrls/32+((_numCtrls%32)>0)); j++){
				Utils::log("%u ", readSNP->_ctrl0Values[j]);
			}
			Utils::log("\n   controls 1 ");
			for(j=0; j<(_numCtrls/32+((_numCtrls%32)>0)); j++){
				Utils::log("%u ", readSNP->_ctrl1Values[j]);
			}
			Utils::log("\n   controls 2 ");
			for(j=0; j<(_numCtrls/32+((_numCtrls%32)>0)); j++){
				Utils::log("%u ", readSNP->_ctrl2Values[j]);
			}
#endif

			if(((_numSnp+1)*_numEntriesCase) >= _sizeArrCase){
				_resizeHostArrCase(_sizeArrCase);
			}
			memcpy(&_host0Cases[_numSnp*_numEntriesCase], readSNP->_case0Values, _numEntriesCase*sizeof(uint32_t));
			memcpy(&_host1Cases[_numSnp*_numEntriesCase], readSNP->_case1Values, _numEntriesCase*sizeof(uint32_t));
			memcpy(&_host2Cases[_numSnp*_numEntriesCase], readSNP->_case2Values, _numEntriesCase*sizeof(uint32_t));

			if(((_numSnp+1)*_numEntriesCtrl) >= _sizeArrCtrl){
				_resizeHostArrCtrl(_sizeArrCtrl);
			}
			memcpy(&_host0Ctrls[_numSnp*_numEntriesCtrl], readSNP->_ctrl0Values, _numEntriesCtrl*sizeof(uint32_t));
			memcpy(&_host1Ctrls[_numSnp*_numEntriesCtrl], readSNP->_ctrl1Values, _numEntriesCtrl*sizeof(uint32_t));
			memcpy(&_host2Ctrls[_numSnp*_numEntriesCtrl], readSNP->_ctrl2Values, _numEntriesCtrl*sizeof(uint32_t));

			_numSnp++;
	}

	// Reorder the values
	uint32_t* auxArr;
	if(_numEntriesCase>_numEntriesCtrl){
		auxArr = new uint32_t[_numSnp*_numEntriesCase];
	}
	else{
		auxArr = new uint32_t[_numSnp*_numEntriesCtrl];
	}
	int i,j;
	for(i=0; i<_numSnp; i++){
		for(j=0; j<_numEntriesCase; j++){
			auxArr[j*_numSnp+i] = _host0Cases[i*_numEntriesCase+j];
		}
	}
	memcpy(_host0Cases, auxArr, _numSnp*_numEntriesCase*sizeof(uint32_t));
	for(i=0; i<_numSnp; i++){
		for(j=0; j<_numEntriesCase; j++){
			auxArr[j*_numSnp+i] = _host1Cases[i*_numEntriesCase+j];
		}
	}
	memcpy(_host1Cases, auxArr, _numSnp*_numEntriesCase*sizeof(uint32_t));
	for(i=0; i<_numSnp; i++){
		for(j=0; j<_numEntriesCase; j++){
			auxArr[j*_numSnp+i] = _host2Cases[i*_numEntriesCase+j];
		}
	}
	memcpy(_host2Cases, auxArr, _numSnp*_numEntriesCase*sizeof(uint32_t));
	for(i=0; i<_numSnp; i++){
		for(j=0; j<_numEntriesCtrl; j++){
			auxArr[j*_numSnp+i] = _host0Ctrls[i*_numEntriesCtrl+j];
		}
	}
	memcpy(_host0Ctrls, auxArr, _numSnp*_numEntriesCtrl*sizeof(uint32_t));
	for(i=0; i<_numSnp; i++){
		for(j=0; j<_numEntriesCtrl; j++){
			auxArr[j*_numSnp+i] = _host1Ctrls[i*_numEntriesCtrl+j];
		}
	}
	memcpy(_host1Ctrls, auxArr, _numSnp*_numEntriesCtrl*sizeof(uint32_t));
	for(i=0; i<_numSnp; i++){
		for(j=0; j<_numEntriesCtrl; j++){
			auxArr[j*_numSnp+i] = _host2Ctrls[i*_numEntriesCtrl+j];
		}
	}
	memcpy(_host2Ctrls, auxArr, _numSnp*_numEntriesCtrl*sizeof(uint32_t));

	delete [] auxArr;

	if(_numSnp){
		_moreDouble = true;
	}
}

void GPUSNPDistributor::_resizeHostArrCase(size_t nsize) {
	if (nsize < _sizeArrCase) {
		return;
	}

	// Allocate a new buffer
	_sizeArrCase = nsize * 2;
	uint32_t* nbuffer;
	cudaMallocHost(&nbuffer, _sizeArrCase*sizeof(uint32_t));
	myCheckCudaError;
	if (!nbuffer) {
		Utils::exit("Memory reallocation failed in file %s in line %d\n",
				__FUNCTION__, __LINE__);
	}
	// Copy the old data
	memcpy(nbuffer, _host0Cases, nsize*sizeof(uint32_t));

	// Release the old buffer
	cudaFreeHost(_host0Cases);
	myCheckCudaError;
	_host0Cases = nbuffer;

	uint32_t* nbuffer1;
	cudaMallocHost(&nbuffer1, _sizeArrCase*sizeof(uint32_t));
	myCheckCudaError;
	if (!nbuffer1) {
		Utils::exit("Memory reallocation failed in file %s in line %d\n",
				__FUNCTION__, __LINE__);
	}
	// Copy the old data
	memcpy(nbuffer1, _host1Cases, nsize*sizeof(uint32_t));
	// Release the old buffer
	cudaFreeHost(_host1Cases);
	myCheckCudaError;
	_host1Cases = nbuffer1;

	uint32_t* nbuffer2;
	cudaMallocHost(&nbuffer2, _sizeArrCase*sizeof(uint32_t));
	myCheckCudaError;
	if (!nbuffer2) {
		Utils::exit("Memory reallocation failed in file %s in line %d\n",
				__FUNCTION__, __LINE__);
	}
	// Copy the old data
	memcpy(nbuffer2, _host2Cases, nsize*sizeof(uint32_t));

	// Release the old buffer
	cudaFreeHost(_host2Cases);
	myCheckCudaError;
	_host2Cases = nbuffer2;
}


void GPUSNPDistributor::_resizeHostArrCtrl(size_t nsize) {
	if (nsize < _sizeArrCtrl) {
		return;
	}

	// Allocate a new buffer
	_sizeArrCtrl = nsize * 2;
	uint32_t* nbuffer;
	cudaMallocHost(&nbuffer, _sizeArrCtrl*sizeof(uint32_t));
	myCheckCudaError;
	if (!nbuffer) {
		Utils::exit("Memory reallocation failed in file %s in line %d\n",
				__FUNCTION__, __LINE__);
	}
	// Copy the old data
	memcpy(nbuffer, _host0Ctrls, nsize*sizeof(uint32_t));

	// Release the old buffer
	cudaFreeHost(_host0Ctrls);
	myCheckCudaError;
	_host0Ctrls = nbuffer;

	uint32_t* nbuffer1;
	cudaMallocHost(&nbuffer1, _sizeArrCtrl*sizeof(uint32_t));
	myCheckCudaError;
	if (!nbuffer1) {
		Utils::exit("Memory reallocation failed in file %s in line %d\n",
				__FUNCTION__, __LINE__);
	}
	// Copy the old data
	memcpy(nbuffer1, _host1Ctrls, nsize*sizeof(uint32_t));
	// Release the old buffer
	cudaFreeHost(_host1Ctrls);
	myCheckCudaError;
	_host1Ctrls = nbuffer1;

	uint32_t* nbuffer2;
	cudaMallocHost(&nbuffer2, _sizeArrCtrl*sizeof(uint32_t));
	myCheckCudaError;
	if (!nbuffer2) {
		Utils::exit("Memory reallocation failed in file %s in line %d\n",
				__FUNCTION__, __LINE__);
	}
	// Copy the old data
	memcpy(nbuffer2, _host2Ctrls, nsize*sizeof(uint32_t));

	// Release the old buffer
	cudaFreeHost(_host2Ctrls);
	myCheckCudaError;
	_host2Ctrls = nbuffer2;
}

uint32_t GPUSNPDistributor::_getPairsSNPsNoLock(uint2* ids, uint64_t& totalAnal){

	uint32_t id1;
	uint32_t id2;

	if(_moreDouble){
		uint16_t iter_block = 0;
		while(iter_block < NUM_PAIRS_BLOCK){

			id1 = _iterDoubleSnp1;
			id2 = _iterDoubleSnp2;

			totalAnal += _numSnp-_iterDoubleSnp2-1;

			ids[iter_block].x = id1;
			ids[iter_block].y = id2;

			iter_block++;

			// Look for the next pair
			if(_iterDoubleSnp2 == _numSnp-2){
				_iterDoubleSnp1++;
				_iterDoubleSnp2 = _iterDoubleSnp1+1;
			}
			else{
				_iterDoubleSnp2++;
			}

			if(_iterDoubleSnp1 == _numSnp-2){ // We have finished to compute the block
				_moreDouble = false;
				break;
			}
		}

		return iter_block;
	}
	return 0;
}

uint32_t GPUSNPDistributor::_getPairsSNPsLock(uint2* ids, uint64_t& totalAnal){

	uint32_t id1;
	uint32_t id2;

	_lock();

	if(_moreDouble){
		uint16_t iter_block = 0;
		while(iter_block < NUM_PAIRS_BLOCK){

			id1 = _iterDoubleSnp1;
			id2 = _iterDoubleSnp2;

			totalAnal += _numSnp-_iterDoubleSnp2-1;

			ids[iter_block].x = id1;
			ids[iter_block].y = id2;

			iter_block++;

			// Look for the next pair
			if(_iterDoubleSnp2 == _numSnp-2){
				_iterDoubleSnp1++;
				_iterDoubleSnp2 = _iterDoubleSnp1+1;
			}
			else{
				_iterDoubleSnp2++;
			}

			if(_iterDoubleSnp1 == _numSnp-2){ // We have finished to compute the block
				_moreDouble = false;
				break;
			}
		}

		_unlock();
		return iter_block;
	}
	_unlock();
	return 0;
}