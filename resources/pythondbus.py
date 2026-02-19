import asyncio
from dbus_next.aio import MessageBus
from dbus_next import Variant

async def main():
    bus = await MessageBus().connect()

    introspection = await bus.introspect(
        'org.freedesktop.Notifications',
        '/org/freedesktop/Notifications'
    )
    obj = bus.get_proxy_object(
        'org.freedesktop.Notifications',
        '/org/freedesktop/Notifications',
        introspection
    )
    iface = obj.get_interface('org.freedesktop.Notifications')

    # Send notification with action button
    notification_id = await iface.call_notify(
        'MyApp',                  # app_name
        0,                        # replaces_id
        '',                       # icon
        'Title',                  # summary
        'Click a button',         # body
        ['ok', 'OK', 'cancel', 'Cancel'],  # actions
        {},                       # hints
        -1                        # timeout
    )

    def action_invoked(id, action_key):
        if id == notification_id:
            print("User clicked:", action_key)
            asyncio.get_event_loop().stop()

    iface.on_action_invoked(action_invoked)

    await asyncio.get_event_loop().create_future()

asyncio.run(main())

