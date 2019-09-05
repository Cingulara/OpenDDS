#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

#include <dds/DdsDcpsInfrastructureC.h>
#include <dds/DdsDcpsPublicationC.h>
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/WaitSet.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#include "BenchTypeSupportImpl.h"
#include "PropertyStatBlock.h"

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#pragma GCC diagnostic pop

using namespace Bench::NodeController;

const int DOMAIN = 89;
const std::string CONFIG_TOPIC_NAME = "Node_Controller_Config";
const std::string REPORT_TOPIC_NAME = "Node_Controller_Report";

std::string dds_root;

std::string
read_file(const std::string& name)
{
  std::stringstream ss;
  std::ifstream file(name);
  if (file.is_open()) {
    ss << file.rdbuf();
  } else {
    std::cerr << "Couldn't open " << name << std::endl;
  }
  return ss.str();
}

bool
json_2_report(std::istream& is, Bench::WorkerReport& report)
{
  rapidjson::Document document;
  rapidjson::IStreamWrapper isw(is);
  document.ParseStream(isw);
  if (!document.IsObject()) {
    std::cerr << "Expected report file to contain JSON document object" << std::endl;
    return false;
  }

  OpenDDS::DCPS::copyFromRapidJson(document, report);

  return true;
}

int
ACE_TMAIN(int argc, ACE_TCHAR* argv[])
{
  dds_root = ACE_OS::getenv("DDS_ROOT");
  if (dds_root.empty()) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("DDS_ROOT isn't defined\n")));
    return 1;
  }

  // DDS Entities
  DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
  DDS::DomainParticipant_var participant = dpf->create_participant(
    DOMAIN, PARTICIPANT_QOS_DEFAULT, 0,
    OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!participant) {
    std::cerr << "create_participant failed" << std::endl;
    return 1;
  }
  ConfigTypeSupport_var config_ts = new ConfigTypeSupportImpl;
  if (config_ts->register_type(participant, "")) {
    std::cerr << "register_type failed for Config" << std::endl;
    return 1;
  }
  DDS::Topic_var config_topic = participant->create_topic(
    CONFIG_TOPIC_NAME.c_str(), config_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0,
    OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!config_topic) {
    std::cerr << "create_topic config failed" << std::endl;
    return 1;
  }
  ReportTypeSupport_var report_ts = new ReportTypeSupportImpl;
  if (report_ts->register_type(participant, "")) {
    std::cerr << "register_type failed for Report" << std::endl;
    return 1;
  }
  DDS::Topic_var report_topic = participant->create_topic(
    REPORT_TOPIC_NAME.c_str(), report_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0,
    OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!report_topic) {
    std::cerr << "create_topic report failed" << std::endl;
    return 1;
  }
  DDS::Publisher_var publisher = participant->create_publisher(
    PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!publisher) {
    std::cerr << "create_publisher failed" << std::endl;
    return 1;
  }
  DDS::DataWriterQos dw_qos;
  publisher->get_default_datawriter_qos(dw_qos);
  dw_qos.history.kind = DDS::KEEP_ALL_HISTORY_QOS;
  dw_qos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;
  DDS::DataWriter_var config_writer = publisher->create_datawriter(
    config_topic, dw_qos, 0,
    OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!config_writer) {
    std::cerr << "create_datawriter config failed" << std::endl;
    return 1;
  }
  ConfigDataWriter_var config_writer_impl = ConfigDataWriter::_narrow(config_writer);
  if (!config_writer_impl) {
    std::cerr << "narrow writer config failed" << std::endl;
    return 1;
  }
  DDS::Subscriber_var subscriber = participant->create_subscriber(
    SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!subscriber) {
    std::cerr << "create_subscriber failed" << std::endl;
    return 1;
  }
  DDS::DataReaderQos dr_qos;
  subscriber->get_default_datareader_qos(dr_qos);
  dr_qos.history.kind = DDS::KEEP_ALL_HISTORY_QOS;
  dr_qos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;
  DDS::DataReader_var report_reader = subscriber->create_datareader(
    report_topic, dr_qos, 0,
    OpenDDS::DCPS::DEFAULT_STATUS_MASK);
  if (!report_reader) {
    std::cerr << "create_datareader report failed" << std::endl;
    return 1;
  }
  ReportDataReader_var report_reader_impl = ReportDataReader::_narrow(report_reader);
  if (!report_reader_impl) {
    std::cerr << "narrow report reader failed" << std::endl;
    return 1;
  }

  ACE_OS::sleep(1);

  WorkerConfig worker;
  Config config;
  const std::string configs = dds_root + "/performance-tests/bench/worker/configs/";
  const size_t expected_workers = 3;

  config.node_id = 1;
  config.workers.length(2);

  worker.worker_id = 1;
  const std::string daemon_config = read_file(configs + "jfti_sim_daemon_config.json");
  worker.config = daemon_config.c_str();
  config.workers[0] = worker;

  worker.worker_id = 2;
  const std::string worker_config = read_file(configs + "jfti_sim_worker_config.json");
  worker.config = worker_config.c_str();
  config.workers[1] = worker;

  if (config_writer_impl->write(config, DDS::HANDLE_NIL)) {
    std::cerr << "1st write failed" << std::endl;
    return 1;
  }

  config.node_id = 2;
  config.workers.length(1);
  worker.worker_id = 1;
  std::string master_config = read_file(configs + "jfti_sim_master_config.json");
  worker.config = master_config.c_str();
  config.workers[0] = worker;
  if (config_writer_impl->write(config, DDS::HANDLE_NIL)) {
    std::cerr << "2nd write failed" << std::endl;
    return 1;
  }

  size_t reports_received = 0;
  std::vector<Bench::WorkerReport> parsed_reports;
  while (reports_received < expected_workers) {
    DDS::ReturnCode_t rc;

    DDS::ReadCondition_var read_condition = report_reader_impl->create_readcondition(
        DDS::ANY_SAMPLE_STATE, DDS::ANY_VIEW_STATE, DDS::ANY_INSTANCE_STATE);
    DDS::WaitSet_var ws = new DDS::WaitSet;
    ws->attach_condition(read_condition);
    DDS::ConditionSeq active;
    rc = ws->wait(active, {DDS::DURATION_INFINITE_SEC, DDS::DURATION_INFINITE_NSEC});
    ws->detach_condition(read_condition);
    report_reader_impl->delete_readcondition(read_condition);
    if (rc != DDS::RETCODE_OK) {
      std::cerr << "Wait failed" << std::endl;
      return 1;
    }

    ReportSeq reports;
    DDS::SampleInfoSeq info;
    rc = report_reader_impl->take(
      reports, info,
      DDS::LENGTH_UNLIMITED,
      DDS::ANY_SAMPLE_STATE,
      DDS::ANY_VIEW_STATE,
      DDS::ANY_INSTANCE_STATE);
    if (rc != DDS::RETCODE_OK) {
      std::cerr << "Take failed" << std::endl;
      return 1;
    }

    for (size_t r = 0; r < reports.length(); r++) {
      if (info[r].valid_data) {
        reports_received += 1;
        if (reports[r].failed) {
          std::cerr << "Worker " << reports[r].worker_id << " of node "
            << reports[r].node_id << " failed" << std::endl;
          return 1;
        } else {
          Bench::WorkerReport report;
          std::stringstream ss;
          ss << reports[r].details << std::flush;
          if (json_2_report(ss, report)) {
            parsed_reports.push_back(report);
          } else {
            std::cerr << "Error parsing report details for node " << reports[r].node_id << ", worker " << reports[r].worker_id << std::endl;
            return 1;
          }
        }
      }
    }
  }

  using Builder::ZERO;

  Builder::TimeStamp max_construction_time = ZERO;
  Builder::TimeStamp max_enable_time = ZERO;
  Builder::TimeStamp max_start_time = ZERO;
  Builder::TimeStamp max_stop_time = ZERO;
  Builder::TimeStamp max_destruction_time = ZERO;

  size_t total_undermatched_readers = 0;
  size_t total_undermatched_writers = 0;
  Builder::TimeStamp max_discovery_time_delta = ZERO;

  Bench::SimpleStatBlock consolidated_latency_stats;
  Bench::SimpleStatBlock consolidated_jitter_stats;
  Bench::SimpleStatBlock consolidated_round_trip_latency_stats;
  Bench::SimpleStatBlock consolidated_round_trip_jitter_stats;

  for (size_t r = 0; r < parsed_reports.size(); ++r) {

    const Bench::WorkerReport& worker_report = parsed_reports[r];

    max_construction_time = std::max(max_construction_time, worker_report.construction_time);
    max_enable_time = std::max(max_enable_time, worker_report.enable_time);
    max_start_time = std::max(max_start_time, worker_report.start_time);
    max_stop_time = std::max(max_stop_time, worker_report.stop_time);
    max_destruction_time = std::max(max_destruction_time, worker_report.destruction_time);

    total_undermatched_readers += worker_report.undermatched_readers;
    total_undermatched_writers += worker_report.undermatched_writers;
    max_discovery_time_delta = std::max(max_discovery_time_delta, worker_report.max_discovery_time_delta);

    const Builder::ProcessReport& process_report = worker_report.process_report;

    for (CORBA::ULong i = 0; i < process_report.participants.length(); ++i) {
      for (CORBA::ULong j = 0; j < process_report.participants[i].subscribers.length(); ++j) {
        for (CORBA::ULong k = 0; k < process_report.participants[i].subscribers[j].datareaders.length(); ++k) {

          const Builder::DataReaderReport& dr_report = process_report.participants[i].subscribers[j].datareaders[k];

          Bench::ConstPropertyStatBlock dr_latency(dr_report.properties, "latency");
          Bench::ConstPropertyStatBlock dr_jitter(dr_report.properties, "jitter");
          Bench::ConstPropertyStatBlock dr_round_trip_latency(dr_report.properties, "round_trip_latency");
          Bench::ConstPropertyStatBlock dr_round_trip_jitter(dr_report.properties, "round_trip_jitter");

          consolidated_latency_stats = consolidate(consolidated_latency_stats, dr_latency.to_simple_stat_block());
          consolidated_jitter_stats = consolidate(consolidated_jitter_stats, dr_jitter.to_simple_stat_block());
          consolidated_round_trip_latency_stats = consolidate(consolidated_round_trip_latency_stats, dr_round_trip_latency.to_simple_stat_block());
          consolidated_round_trip_jitter_stats = consolidate(consolidated_round_trip_jitter_stats, dr_round_trip_jitter.to_simple_stat_block());
        }
      }
    }
  }

  std::cout << std::endl;

  std::cout << "Test Timing Stats:" << std::endl;
  std::cout << "  Max Construction Time: " << max_construction_time << " seconds" << std::endl;
  std::cout << "  Max Enable Time: " << max_enable_time << " seconds" << std::endl;
  std::cout << "  Max Start Time: " << max_start_time << " seconds" << std::endl;
  std::cout << "  Max Stop Time: " << max_stop_time << " seconds" << std::endl;
  std::cout << "  Max Destruction Time: " << max_destruction_time << " seconds" << std::endl;

  std::cout << std::endl;

  std::cout << "Discovery Stats:" << std::endl;
  std::cout << "  Total Undermatched Readers: " << total_undermatched_readers << ", Total Undermatched Writers: " << total_undermatched_writers << std::endl;
  std::cout << "  Max Discovery Time Delta: " << max_discovery_time_delta << " seconds" << std::endl;

  std::cout << std::endl;

  consolidated_latency_stats.pretty_print(std::cout, "latency");

  std::cout << std::endl;

  consolidated_jitter_stats.pretty_print(std::cout, "jitter");

  std::cout << std::endl;

  consolidated_round_trip_latency_stats.pretty_print(std::cout, "round trip latency");

  std::cout << std::endl;

  consolidated_round_trip_jitter_stats.pretty_print(std::cout, "round trip jitter");

  // Clean up OpenDDS
  participant->delete_contained_entities();
  dpf->delete_participant(participant);
  TheServiceParticipant->shutdown();

  return 0;
}