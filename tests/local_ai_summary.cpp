#include "docwire.h"
#include <sstream>

int main(int argc, char* argv[])
{
  using namespace docwire;
  std::stringstream out_stream;

  try
  {
    std::filesystem::path("data_processing_definition.doc") | content_type::detector{} | office_formats_parser{} | PlainTextExporter() | local_ai::model_chain_element("Write a short summary for this text:\n\n") | out_stream;
    #ifdef NDEBUG
      ensure(out_stream.str()) == "Data processing is the collection, organization, analysis, and interpretation of data to extract useful insights and support decision-making.";
    #else
      ensure(out_stream.str()) == "Data processing is the process of transforming raw data into meaningful information.";
    #endif
  }
  catch (const std::exception& e)
  {
    std::cerr << errors::diagnostic_message(e) << std::endl;
    return 1;
  }

  return 0;
}
