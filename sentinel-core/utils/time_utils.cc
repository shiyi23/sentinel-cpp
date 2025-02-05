#include "time_utils.h"

#include <iostream>

/**
 * <p>Provides millisecond-level time of OS.</p>
 * <p>
 * Here we should see that not all the time TimeUtil should
 * keep looping 1_000 times every second (Actually about 800/s due to some losses).
 * <pre>
 * * In idle conditions it just acts as System.currentTimeMillis();
 * * In busy conditions (significantly more than 1_000/s) it keeps loop to reduce costs.
 * </pre>
 * For detail design and proposals please goto
 * <a href="https://github.com/alibaba/Sentinel/issues/1702#issuecomment-692151160">https://github.com/alibaba/Sentinel/issues/1702</a>
 * @author alibaba.com
 * @author huangshiyi
 * */

namespace Sentinel {
namespace Utils {

std::chrono::milliseconds TimeUtils::CurrentTimeMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
}

TimeUtils::TimeUtils()
{
//  statistics_ = new Stat::BucketLeapArray(3, 3000);
  currentTimeMillis_ = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  std::thread t(&TimeUtils::Run, this);
  t.detach();
}

void TimeUtils::Run()
{
  while(true)
  {
    this->Check();
    if(this->state_ == STATE::RUNNING)
    {
      this->currentTimeMillis_ = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch());
      this->statistics_->CurrentWindow(currentTimeMillis_.count())->Value()->get_writes().fetch_add(1);
      try {
        usleep(1000);//1000 microseconds = 1ms
      } catch (std::exception e)
      {
        throw e.what();
      }
      continue ;
    }

    if(this->state_ == STATE::IDLE)
    {
      try {
        usleep(300000);//300000 micro seconds = 300 ms
      } catch (std::exception e)
      {
        throw e.what();
      }
      continue ;
    }

    if(this->state_ == STATE::PREPARE)
    {
      //TODO:should print debug level log info here, but I don't have too much time now
      std::cout << "TimeUtils switches to RUNNING" << std::endl;
      this->currentTimeMillis_ = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch());
      this->state_ = STATE::RUNNING;
    }

  }
}

void TimeUtils::Check()
{
  std::chrono::milliseconds now = this->CurrentTime(true);
  //every period
  if(now - this->last_check_ < CHECK_INTERVAL_)
  {
    return ;
  }
  this->last_check_ = now;
  std::pair<long, long> qps = this->get_current_qps(now);
  if(this->state_ == STATE::IDLE && qps.first > HITS_UPPER_BOUNDARY_)
  {
    std::cout << "TimeUtil switches to PREPARE for better performance, "
                 "reads= << " << qps.first << "writes= << " << qps.second << std::endl;
    this->state_ = STATE::PREPARE;
  } else if(this->state_ == STATE::RUNNING && qps.first < HITS_LOWER_BOUNDARY_)
  {
    std::cout << "TimeUtil switches to IDLE due to not enough load, "
                 "reads= " << qps.first << "writes= " << qps.second <<std::endl;
    this->state_ = STATE::IDLE;
  }
}

std::pair<long, long> TimeUtils::get_current_qps(std::chrono::milliseconds now)
{
  auto vec = this->statistics_->Buckets();
  long reads = 0, writes = 0;
  int cnt = 0;
  for (auto const elem : vec)
  {
    if (elem->IsTimeInBucket(now.count()))
    {
      continue ;
    }
    ++ cnt;
    reads += elem->Value()->get_reads().load();
    writes += elem->Value()->get_writes().load();
    if (cnt < 1)
    {
      return std::make_pair(0,0);
    }
  }
  return std::make_pair(reads/cnt, writes/cnt);
}

std::chrono::milliseconds TimeUtils::CurrentTime(bool inner_call)
{
  auto now = this->currentTimeMillis_;
  auto val = this->statistics_->CurrentWindow()->Value();
  if(!inner_call)
  {
    //external call
    val->get_reads().fetch_add(1);
  }
  if(this->state_ == STATE::IDLE || this->state_ == STATE::PREPARE)
  {
    now =  std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    this->currentTimeMillis_ = now; //cache time
    if(!inner_call)
    {
      val->get_writes().fetch_add(1);
    }
  }
  return now;
}

}  // namespace Utils
}  // namespace Sentinel
