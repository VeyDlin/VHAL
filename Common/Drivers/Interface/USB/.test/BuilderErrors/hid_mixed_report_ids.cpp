// expect: reports with and without IDs cannot be mixed
#include <Drivers/Interface/USB/Class/HID/USBHidReportBuilder.h>

using Report = USBHidReportDescriptor<+[] {
	return USBHidReportBuilder()
		.UsagePage(0x01)
		.Usage(0x06)
		.Collection(USBHidCollection::Application)
		.ReportSize(8)
		.ReportCount(1)
		.Input(USBHidFlags::Data)
		.ReportId(2)
		.Input(USBHidFlags::Data)
		.EndCollection();
}>;

static_assert(Report::Info.reportCount == 2);
