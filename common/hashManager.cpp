
#include "hashManager.h"
#include <assert.h>
#include <iostream>
using namespace std;

HashtableManager::HashtableManager()
{
	m_pmtx = new Mutex();
	assert(m_pmtx != NULL);
}

HashtableManager::~HashtableManager()
{
	if(m_pght)
		ght_finalize(m_pght);
	if(m_pmtx)
	{
		delete m_pmtx;
		m_pmtx = NULL;
	}
}	

int HashtableManager::Create(unsigned int hashnum)
{
	m_pght = ght_create(hashnum);
	assert(m_pght != NULL);
	return 0;
}

int HashtableManager::Insert(string key, string value)
{
	m_pmtx->Lock();
//	int ret = ght_insert(m_pght, (char*)value.c_str(), key.length(), key.c_str());
	m_mapHash[key] = value;
	m_pmtx->Unlock();
	return 0;
}

int HashtableManager::Search(string key, string& value)
{
/*
	m_pmtx->Lock();
	void* v = NULL;
	if((v = ght_get(m_pght, key.length(), key.c_str())) == NULL)
	{
		m_pmtx->Unlock();
		return -1;
	}
	cout<<"key "<<key<<" search hash value:"<<(char*)v<<endl;
	value = (char*)v;
	m_pmtx->Unlock();
*/
	value = m_mapHash[key];	
	return 0;
}

int HashtableManager::Delete(string key)
{
/*
	m_pmtx->Lock();
	void* v = NULL;
	if((v = ght_remove(m_pght, key.length(), (char*)key.c_str())) == NULL)
	{
		m_pmtx->Unlock();
		return -1;
	}
	m_pmtx->Unlock();
*/
	return 0;
}
