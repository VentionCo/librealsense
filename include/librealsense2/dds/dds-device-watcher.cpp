// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2022 Intel Corporation. All Rights Reserved.

#include "dds-device-watcher.h"
#include "dds-topic.h"
#include "dds-topic-reader.h"
#include "dds-device.h"
#include "dds-utilities.h"
#include "dds-guid.h"
#include "topics/device-info/device-info-msg.h"
#include "topics/device-info/deviceInfoPubSubTypes.h"

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>


using namespace eprosima::fastdds::dds;
using namespace librealsense::dds;


dds_device_watcher::dds_device_watcher( std::shared_ptr< dds::dds_participant > const & participant )
    : _device_info_topic( new dds_topic_reader( std::make_shared< dds_topic >(
        participant, TypeSupport( new topics::device_info::type ), topics::device_info::TOPIC_NAME ) ) )
    , _participant( participant )
    , _active_object( [this]( dispatcher::cancellable_timer timer ) {

        eprosima::fastrtps::Duration_t one_second = { 1, 0 };
        if( _device_info_topic->get()->wait_for_unread_message( one_second ) )
        {
            dds::topics::raw::device_info raw_data;
            SampleInfo info;
            // Process all the samples until no one is returned,
            // We will distinguish info change vs new data by validating using `valid_data` field
            while( ReturnCode_t::RETCODE_OK == _device_info_topic->get()->take_next_sample( &raw_data, &info ) )
            {
                // Only samples for which valid_data is true should be accessed
                // valid_data indicates that the instance is still ALIVE and the `take` return an
                // updated sample
                if( info.valid_data )
                {
                    dds::topics::device_info device_info = raw_data;

                    eprosima::fastrtps::rtps::GUID_t guid;
                    eprosima::fastrtps::rtps::iHandle2GUID( guid, info.publication_handle );

                    LOG_DEBUG( "DDS device (" << _participant->print(guid) << ") detected:"
                        << "\n\tName: " << device_info.name
                        << "\n\tSerial: " << device_info.serial
                        << "\n\tProduct line: " << device_info.product_line
                        << "\n\tTopic root: " << device_info.topic_root
                        << "\n\tLocked: " << ( device_info.locked ? "yes" : "no" ) );

                    // Add a new device record into our dds devices map
                    auto device = dds::dds_device::create( _participant, guid, device_info );
                    {
                        std::lock_guard< std::mutex > lock( _devices_mutex );
                        _dds_devices[guid] = device;
                    }

                    // NOTE: device removals are handled via the writer-removed notification; see the
                    // listener callback in init().
                    if( _on_device_added )
                        _on_device_added( device );
                }
            }
        }
    } )
{
    if( ! _participant->is_valid() )
        throw std::runtime_error( "participant was not initialized" );
}

void dds_device_watcher::start()
{
    stop();
    if( ! _device_info_topic->is_running() )
    {
        // Get all sensors & profiles data
        // TODO: not sure we want to do it here in the C'tor, 
        // it takes time and keeps the 'dds_device_server' busy
        init();
    }
    _active_object.start();
    LOG_DEBUG( "DDS device watcher started" );
}

void dds_device_watcher::stop()
{
    if( ! is_stopped() )
    {
        _active_object.stop();
        //_callback_inflight.wait_until_empty();
        LOG_DEBUG( "DDS device watcher stopped" );
    }
}


dds_device_watcher::~dds_device_watcher()
{
    stop();
}

void dds_device_watcher::init()
{
    if( ! _listener )
        _participant->create_listener( &_listener )->on_writer_removed( [this]( dds::dds_guid guid, char const * ) {
            std::shared_ptr< dds_device > device;
            {
                std::lock_guard< std::mutex > lock( _devices_mutex );
                auto it = _dds_devices.find( guid );
                if( it != _dds_devices.end() )
                {
                    device = it->second;
                    auto serial_number = device->device_info().serial;
                    _dds_devices.erase( it );
                }
            }
            if( device && _on_device_removed )
                _on_device_removed( device );  // must happen outside the mutex
        } );

    if( ! _device_info_topic->is_running() )
        _device_info_topic->run();

    LOG_DEBUG( "DDS device watcher initialized successfully" );
}


bool dds_device_watcher::foreach_device(
    std::function< bool( std::shared_ptr< dds::dds_device > const & ) > fn ) const
{
    std::lock_guard< std::mutex > lock( _devices_mutex );
    for( auto && guid_to_dev_info : _dds_devices )
    {
        if( ! fn( guid_to_dev_info.second ) )
            return false;
    }
    return true;
}

