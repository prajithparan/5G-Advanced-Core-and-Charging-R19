// Unit tests for libs/bss-sid's product.hpp -- real TMF620 field shapes, confirmed by directly
// downloading and parsing TM Forum's own public swagger JSON (see product.hpp's own file header).

#include "bss_sid/product.hpp"

#include <gtest/gtest.h>

TEST(BssSidProduct, MoneyJsonRoundTrips) {
    bss_sid::Money original{.unit = "USD", .value = 9.99};
    const nlohmann::json j = original;
    EXPECT_EQ(j.at("unit"), "USD");
    EXPECT_DOUBLE_EQ(j.at("value").get<double>(), 9.99);
    const auto decoded = j.get<bss_sid::Money>();
    EXPECT_EQ(decoded.unit, original.unit);
    EXPECT_DOUBLE_EQ(*decoded.value, *original.value);
}

TEST(BssSidProduct, TimePeriodOmitsUnsetFields) {
    bss_sid::TimePeriod period{};
    period.startDateTime = "2026-01-01T00:00:00.000Z";
    const nlohmann::json j = period;
    EXPECT_TRUE(j.contains("startDateTime"));
    EXPECT_FALSE(j.contains("endDateTime"));
}

TEST(BssSidProduct, ProductOfferingPriceRoundTrips) {
    bss_sid::ProductOfferingPrice original{};
    original.id = "pop-1";
    original.name = "5GB Monthly Data";
    original.priceType = "recurring";
    original.price = bss_sid::Money{.unit = "USD", .value = 20.0};
    original.recurringChargePeriodType = "month";
    original.recurringChargePeriodLength = 1;
    original.unitOfMeasure = bss_sid::Quantity{.amount = 5, .units = "GB"};

    const nlohmann::json j = original;
    const auto decoded = j.get<bss_sid::ProductOfferingPrice>();

    ASSERT_TRUE(decoded.price.has_value());
    EXPECT_EQ(decoded.price->unit, "USD");
    EXPECT_DOUBLE_EQ(*decoded.price->value, 20.0);
    ASSERT_TRUE(decoded.unitOfMeasure.has_value());
    EXPECT_EQ(decoded.unitOfMeasure->units, "GB");
    EXPECT_EQ(decoded.recurringChargePeriodType, "month");
}

TEST(BssSidProduct, ProductOfferingReferencesPricesById) {
    bss_sid::ProductOffering offering{};
    offering.name = "5G Standard Plan";
    offering.isSellable = true;
    offering.productOfferingPrice.push_back(
        bss_sid::ProductOfferingPriceRef{.id = "pop-1", .name = "5GB Monthly Data"});

    const nlohmann::json j = offering;
    const auto decoded = j.get<bss_sid::ProductOffering>();

    ASSERT_EQ(decoded.productOfferingPrice.size(), 1u);
    EXPECT_EQ(decoded.productOfferingPrice[0].id, "pop-1");
    EXPECT_TRUE(*decoded.isSellable);
}

TEST(BssSidProduct, ProductOfferingOmitsEmptyPriceArray) {
    bss_sid::ProductOffering offering{};
    offering.name = "Bare Offering";
    const nlohmann::json j = offering;
    EXPECT_FALSE(j.contains("productOfferingPrice"));
}

// Extended 2026-08-10 (docs/DATA_MODEL.md E2 / ADR-0053/0054): real TMF620 fields added for the
// 5G SA enterprise/consumer use case -- category is now the real CategoryRef[] shape (replacing
// the earlier vector<string> simplification), and prodSpecCharValueUse is the key mechanism for
// configurable product characteristics (e.g. S-NSSAI, 5QI on an enterprise slice offering).

TEST(BssSidProduct, ProductOfferingCategoryIsRealCategoryRefShape) {
    bss_sid::ProductOffering offering{};
    offering.name = "Enterprise Slice";
    offering.category.push_back(bss_sid::CategoryRef{.id = "cat-1", .name = "Enterprise"});

    const nlohmann::json j = offering;
    ASSERT_TRUE(j.contains("category"));
    EXPECT_EQ(j.at("category")[0].at("id"), "cat-1");
    EXPECT_EQ(j.at("category")[0].at("name"), "Enterprise");

    const auto decoded = j.get<bss_sid::ProductOffering>();
    ASSERT_EQ(decoded.category.size(), 1u);
    EXPECT_EQ(decoded.category[0].id, "cat-1");
}

TEST(BssSidProduct, ProdSpecCharValueUseRoundTripsConfigurableCharacteristics) {
    bss_sid::ProductOffering offering{};
    offering.name = "Manufacturing Slice";

    bss_sid::ProductSpecificationCharacteristicValueUse snssai{};
    snssai.id = "char-snssai";
    snssai.name = "S-NSSAI";
    snssai.valueType = "string";
    bss_sid::CharacteristicValueSpecification value{};
    value.value = "1-DEADBE";
    value.isDefault = true;
    snssai.productSpecCharacteristicValue.push_back(value);
    offering.prodSpecCharValueUse.push_back(snssai);

    const nlohmann::json j = offering;
    const auto decoded = j.get<bss_sid::ProductOffering>();

    ASSERT_EQ(decoded.prodSpecCharValueUse.size(), 1u);
    EXPECT_EQ(decoded.prodSpecCharValueUse[0].name, "S-NSSAI");
    ASSERT_EQ(decoded.prodSpecCharValueUse[0].productSpecCharacteristicValue.size(), 1u);
    EXPECT_EQ(
        decoded.prodSpecCharValueUse[0].productSpecCharacteristicValue[0].value->get<std::string>(),
        "1-DEADBE");
}

TEST(BssSidProduct, ProductSpecificationCharacteristicRoundTrips) {
    bss_sid::ProductSpecification spec{};
    spec.name = "Private 5G Network Slice";
    spec.lifecycleStatus = "Active";

    bss_sid::ProductSpecificationCharacteristic characteristic{};
    characteristic.id = "char-5qi";
    characteristic.name = "5QI";
    characteristic.configurable = true;
    characteristic.valueType = "number";
    spec.productSpecCharacteristic.push_back(characteristic);

    const nlohmann::json j = spec;
    const auto decoded = j.get<bss_sid::ProductSpecification>();

    ASSERT_EQ(decoded.productSpecCharacteristic.size(), 1u);
    EXPECT_EQ(decoded.productSpecCharacteristic[0].name, "5QI");
    EXPECT_TRUE(*decoded.productSpecCharacteristic[0].configurable);
}
