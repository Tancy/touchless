// Copyright (C) 2012-2014 Leap Motion, Inc. All rights reserved.
#pragma once

#include "var_logic.h"
#include "AutoPacket.h"
#include MEMORY_HEADER

/// <summary>
/// AutoStile provides a means of calling a context as though it is an AutoFilter.
/// The AutoStile Args use the same syntax as AutoFilter:
/// - Input types will be injected in to the slave packet
/// - Output types (auto_out only) will be extracted from the slave packet
/// - The absence of any output types indicates that *all* data from the slave context
/// should be output. This is because AutoPacket& is already an argument, and cannot
/// be repeated.
/// ASSERT: Injection and extraction of all data will not yield multiple decorations,
/// since AutoPacket::ForwardAll prevents this from occurring.
/// NOTE: If shared input types are used data copying will be avoided.
/// IMPORTANT: Unshared output types cannot be used, since they will be available
/// before the execution of any Deferred methods in the slave context.
/// </summary>
/// <remarks>
/// When AutoStile::AutoFilter is called it initiates execution of a Slave context,
/// and injects the objects referenced by AutoFilter into the new context.
/// AutoStile also handles the extraction of data requested from the context.
///
/// If merged data is required from the context then it is necessary to decorate the
/// master_packet with an extraction function, and declare the type of that function
/// as an *input* argument of the AutoStile. The decoration and type are provided by
/// the AutoMergeStile class.
///
/// </remarks>
template<class... Args>
class AutoStile
{
protected:
  std::weak_ptr<AutoPacketFactory> m_slave_factory;

  /// <summary>Decorator for input shares the reference to master_data</summary>
  template<class data_pipe>
  static typename std::enable_if<auto_arg<data_pipe>::is_input, bool>::type DecorationStile(std::shared_ptr<AutoPacket>& slave_packet, std::shared_ptr<AutoPacket>& master_packet, data_pipe& master_data) {
    slave_packet->Decorate(master_data);
    return true; //Place-holder in variadic initializer
  }

  /// <summary>Decorator for output creates an extraction for slave_data</summary>
  template<class data_pipe>
  static typename std::enable_if<auto_arg<data_pipe>::is_output, bool>::type DecorationStile(std::shared_ptr<AutoPacket>& slave_packet, std::shared_ptr<AutoPacket>& master_packet, data_pipe& master_data) {
    static_assert(std::is_same<data_pipe, auto_out<typename auto_arg<data_pipe>::id_type>>::value, "Output types must be auto_out");

    // Reverse argument orientation for AutoFilter in slave context
    typedef auto_in<typename auto_arg<data_pipe>::id_type> slave_in_type;

    slave_packet->AddRecipient<void, slave_in_type>(std::function<void(slave_in_type slave_data)>([&master_data](slave_in_type slave_data) {
      // NOTE: The lambda copy of master_data is implicitly a move of AutoCheckout,
      // so this lambda has sole responsibility for providing the requested data.
      // NOTE: This is a deep copy, not a shared resource.
      *master_data = *slave_data;
    }));
    return true; //Place-holder in variadic initializer
  }

  /// <summary>Inject all data from slave_packet into master_packet</summary>
  static void ForwardStile(std::shared_ptr<AutoPacket>& slave_packet, std::shared_ptr<AutoPacket>& master_packet) {
    slave_packet->AddRecipient<void, const AutoPacket&>(std::function<void(const AutoPacket&)>([master_packet](const AutoPacket& slave_packet) {
      slave_packet.ForwardAll(master_packet);
    }));
  }

public:
  AutoStile(std::weak_ptr<CoreContext> slave_context = std::weak_ptr<CoreContext>()) {
    Leash(slave_context);
  }

  void AutoFilter(AutoPacket& packet, Args... data) {
    std::shared_ptr<AutoPacketFactory> slave_factory = m_slave_factory.lock();
    if (!slave_factory)
      return;

    // Initiate the slave context
    std::shared_ptr<AutoPacket> master_packet = packet.shared_from_this();
    std::shared_ptr<AutoPacket> slave_packet = slave_factory->NewPacket();
    bool init[] = {DecorationStile<Args>(slave_packet, master_packet, data)...,
      false}; //Final argument is required in the case of an empty variadic template
    (void)init;

    if (var_and<auto_arg<Args>::is_input...>::value) {
      ForwardStile(slave_packet, master_packet);
    }
  }

  void Leash(std::weak_ptr<CoreContext> slave_context = std::weak_ptr<CoreContext>()) {
    m_slave_factory.reset();

    std::shared_ptr<CoreContext> strong_context = slave_context.lock();
    if (!strong_context)
      return;

    strong_context->NotifyWhenAutowired<AutoPacketFactory>([this, slave_context](){
      // NOTE: Call-back is not necessarily inside the notifying context.
      std::shared_ptr<CoreContext> strong_context = slave_context.lock();
      if (!strong_context)
        return;
      std::shared_ptr<AutoPacketFactory> slave_factory;
      strong_context->FindByTypeRecursive(slave_factory);
      m_slave_factory = slave_factory;
    });
  }
};
