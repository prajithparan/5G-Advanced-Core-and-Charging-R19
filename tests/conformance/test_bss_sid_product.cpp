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
