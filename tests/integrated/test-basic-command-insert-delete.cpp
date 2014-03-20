/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014,  Regents of the University of California.
 *
 * This file is part of NDN repo-ng (Next generation of NDN repository).
 * See AUTHORS.md for complete list of repo-ng authors and contributors.
 *
 * repo-ng is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * repo-ng is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * repo-ng, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "handles/write-handle.hpp"
#include "handles/delete-handle.hpp"
#include "storage/storage-handle.hpp"
#include "storage/sqlite-handle.hpp"

#include "../sqlite-fixture.hpp"
#include "../dataset-fixtures.hpp"

#include <ndn-cxx/util/command-interest-generator.hpp>

#include <boost/test/unit_test.hpp>

namespace repo {
namespace tests {

using ndn::time::milliseconds;
using ndn::time::seconds;
using ndn::EventId;
namespace random=ndn::random;

//All the test cases in this test suite should be run at once.
BOOST_AUTO_TEST_SUITE(TestBasicCommandInsertDelete)

const static uint8_t content[8] = {3, 1, 4, 1, 5, 9, 2, 6};

template<class Dataset>
class Fixture : public SqliteFixture, public Dataset
{
public:
  Fixture()
    : scheduler(repoFace.getIoService())
    , writeHandle(repoFace, *handle, keyChain, scheduler, validator)
    , deleteHandle(repoFace, *handle, keyChain, scheduler, validator)
    , insertFace(repoFace.getIoService())
    , deleteFace(repoFace.getIoService())
  {
    validator.addInterestRule("^<>",
                              *keyChain.getCertificate(keyChain.getDefaultCertificateName()));
    writeHandle.listen(Name("/repo/command"));
    deleteHandle.listen(Name("/repo/command"));
  }

  ~Fixture()
  {
    repoFace.getIoService().stop();
  }

  void
  scheduleInsertEvent();

  void
  scheduleDeleteEvent();

  void
  onInsertInterest(const Interest& interest);

  void
  onRegisterFailed(const std::string& reason);

  void
  delayedInterest();

  void
  stopFaceProcess();


  void
  onInsertData(const Interest& interest, Data& data);

  void
  onDeleteData(const Interest& interest, Data& data);

  void
  onInsertTimeout(const Interest& interest);

  void
  onDeleteTimeout(const Interest& interest);

  void
  sendInsertInterest(const Interest& interest);

  void
  sendDeleteInterest(const Interest& deleteInterest);

  void
  checkInsertOK(const Interest& interest);

  void
  checkDeleteOK(const Interest& interest);

public:
  Face repoFace;
  Scheduler scheduler;
  CommandInterestValidator validator;
  KeyChain keyChain;
  ndn::CommandInterestGenerator generator;
  WriteHandle writeHandle;
  DeleteHandle deleteHandle;
  Face insertFace;
  Face deleteFace;
  std::map<Name, EventId> insertEvents;
};

template<class T> void
Fixture<T>::onInsertInterest(const Interest& interest)
{
  Data data(Name(interest.getName()));
  data.setContent(content, sizeof(content));
  data.setFreshnessPeriod(milliseconds(0));
  keyChain.sign(data);
  insertFace.put(data);

  std::map<Name, EventId>::iterator event = insertEvents.find(interest.getName());
  if (event != insertEvents.end()) {
    scheduler.cancelEvent(event->second);
    insertEvents.erase(event);
  }
  // schedule an event 50ms later to check whether insert is OK
  scheduler.scheduleEvent(milliseconds(50),
                          bind(&Fixture<T>::checkInsertOK, this, interest));

}


template<class T> void
Fixture<T>::onRegisterFailed(const std::string& reason)
{
  BOOST_ERROR("ERROR: Failed to register prefix in local hub's daemon" + reason);
}

template<class T> void
Fixture<T>::delayedInterest()
{
  BOOST_ERROR("Fetching interest does not come. It may be satisfied in CS or something is wrong");
}

template<class T> void
Fixture<T>::stopFaceProcess()
{
  repoFace.getIoService().stop();
}

template<class T> void
Fixture<T>::onInsertData(const Interest& interest, Data& data)
{
  RepoCommandResponse response;
  response.wireDecode(data.getContent().blockFromValue());
  int statusCode = response.getStatusCode();
  BOOST_CHECK_EQUAL(statusCode, 100);
}

template<class T> void
Fixture<T>::onDeleteData(const Interest& interest, Data& data)
{
  RepoCommandResponse response;
  response.wireDecode(data.getContent().blockFromValue());
  int statusCode = response.getStatusCode();
  BOOST_CHECK_EQUAL(statusCode, 200);

  //schedlute an event to check whether delete is OK.
  scheduler.scheduleEvent(milliseconds(100),
                          bind(&Fixture<T>::checkDeleteOK, this, interest));
}

template<class T> void
Fixture<T>::onInsertTimeout(const Interest& interest)
{
  BOOST_ERROR("Inserert command timeout");
}

template<class T> void
Fixture<T>::onDeleteTimeout(const Interest& interest)
{
  BOOST_ERROR("delete command timeout");
}

template<class T> void
Fixture<T>::sendInsertInterest(const Interest& insertInterest)
{
  insertFace.expressInterest(insertInterest,
                             bind(&Fixture<T>::onInsertData, this, _1, _2),
                             bind(&Fixture<T>::onInsertTimeout, this, _1));
}

template<class T> void
Fixture<T>::sendDeleteInterest(const Interest& deleteInterest)
{
  deleteFace.expressInterest(deleteInterest,
                             bind(&Fixture<T>::onDeleteData, this, _1, _2),
                             bind(&Fixture<T>::onDeleteTimeout, this, _1));
}

template<class T> void
Fixture<T>::checkInsertOK(const Interest& interest)
{
  Data data;
  BOOST_TEST_MESSAGE(interest);
  BOOST_CHECK_EQUAL(handle->readData(interest, data), true);
  int rc = memcmp(data.getContent().value(), content, sizeof(content));
  BOOST_CHECK_EQUAL(rc, 0);
}

template<class T> void
Fixture<T>::checkDeleteOK(const Interest& interest)
{
  Data data;
  BOOST_CHECK_EQUAL(handle->readData(interest, data), false);
}


template<class T> void
Fixture<T>::scheduleInsertEvent()
{
  int timeCount = 1;
  for (typename T::DataContainer::iterator i = this->data.begin();
       i != this->data.end(); ++i) {
    Name insertCommandName("/repo/command/insert");
    RepoCommandParameter insertParameter;
    insertParameter.setName(Name((*i)->getName())
                              .appendNumber(random::generateWord64()));

    insertCommandName.append(insertParameter.wireEncode());
    Interest insertInterest(insertCommandName);
    generator.generateWithIdentity(insertInterest, keyChain.getDefaultIdentity());
    //schedule a job to express insertInterest every 50ms
    scheduler.scheduleEvent(milliseconds(timeCount * 50 + 1000),
                            bind(&Fixture<T>::sendInsertInterest, this, insertInterest));
    //schedule what to do when interest timeout

    EventId delayEventId = scheduler.scheduleEvent(milliseconds(5000 + timeCount * 50),
                                                   bind(&Fixture<T>::delayedInterest, this));
    insertEvents[insertParameter.getName()] = delayEventId;

    //The delayEvent will be canceled in onInsertInterest
    insertFace.setInterestFilter(insertParameter.getName(),
                                 bind(&Fixture<T>::onInsertInterest, this, _2),
                                 bind(&Fixture<T>::onRegisterFailed, this, _2));
    timeCount++;
  }
}


template<class T> void
Fixture<T>::scheduleDeleteEvent()
{
  int timeCount = 1;
  for (typename T::DataContainer::iterator i = this->data.begin();
       i != this->data.end(); ++i) {
    Name deleteCommandName("/repo/command/delete");
    RepoCommandParameter deleteParameter;
    static boost::random::mt19937_64 gen;
    static boost::random::uniform_int_distribution<uint64_t> dist(0, 0xFFFFFFFFFFFFFFFFLL);
    deleteParameter.setProcessId(dist(gen));
    deleteParameter.setName((*i)->getName());
    deleteCommandName.append(deleteParameter.wireEncode());
    Interest deleteInterest(deleteCommandName);
    generator.generateWithIdentity(deleteInterest, keyChain.getDefaultIdentity());
    scheduler.scheduleEvent(milliseconds(4000 + timeCount * 50),
                            bind(&Fixture<T>::sendDeleteInterest, this, deleteInterest));
    timeCount++;
  }
}

BOOST_FIXTURE_TEST_CASE_TEMPLATE(InsertDelete, T, DatasetFixtures, Fixture<T>)
{
  // schedule events
  this->scheduler.scheduleEvent(seconds(0),
                                bind(&Fixture<T>::scheduleInsertEvent, this));
  this->scheduler.scheduleEvent(seconds(10),
                                bind(&Fixture<T>::scheduleDeleteEvent, this));

  // schedule an event to terminate IO
  this->scheduler.scheduleEvent(seconds(20),
                                bind(&Fixture<T>::stopFaceProcess, this));
  this->repoFace.getIoService().run();
}

BOOST_AUTO_TEST_SUITE_END()

} //namespace tests
} //namespace repo