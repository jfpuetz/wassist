"""Echo test: record a few seconds from a microphone into PSRAM and play them back on a speaker."""

from esphome import automation
import esphome.codegen as cg
from esphome.components import microphone, speaker
import esphome.config_validation as cv
from esphome.const import CONF_DURATION, CONF_ID, CONF_MICROPHONE, CONF_SPEAKER

CODEOWNERS = ["@jfpuetz"]
DEPENDENCIES = ["microphone", "speaker"]
AUTO_LOAD = ["audio"]

CONF_GAIN_FACTOR = "gain_factor"
CONF_ON_RECORD_START = "on_record_start"
CONF_ON_PLAYBACK_START = "on_playback_start"
CONF_ON_FINISHED = "on_finished"

echo_test_ns = cg.esphome_ns.namespace("echo_test")
EchoTest = echo_test_ns.class_("EchoTest", cg.Component)
StartAction = echo_test_ns.class_(
    "StartAction", automation.Action, cg.Parented.template(EchoTest)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EchoTest),
        cv.Required(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        cv.Required(CONF_SPEAKER): cv.use_id(speaker.Speaker),
        cv.Optional(CONF_DURATION, default="5s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(
                min=cv.TimePeriod(milliseconds=500), max=cv.TimePeriod(seconds=15)
            ),
        ),
        cv.Optional(CONF_GAIN_FACTOR, default=4): cv.int_range(min=1, max=64),
        cv.Optional(CONF_ON_RECORD_START): automation.validate_automation(single=True),
        cv.Optional(CONF_ON_PLAYBACK_START): automation.validate_automation(
            single=True
        ),
        cv.Optional(CONF_ON_FINISHED): automation.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mic = await cg.get_variable(config[CONF_MICROPHONE])
    cg.add(var.set_microphone(mic))
    spk = await cg.get_variable(config[CONF_SPEAKER])
    cg.add(var.set_speaker(spk))
    cg.add(var.set_duration_ms(config[CONF_DURATION].total_milliseconds))
    cg.add(var.set_gain_factor(config[CONF_GAIN_FACTOR]))

    if conf := config.get(CONF_ON_RECORD_START):
        await automation.build_automation(var.get_record_start_trigger(), [], conf)
    if conf := config.get(CONF_ON_PLAYBACK_START):
        await automation.build_automation(var.get_playback_start_trigger(), [], conf)
    if conf := config.get(CONF_ON_FINISHED):
        await automation.build_automation(var.get_finished_trigger(), [], conf)


@automation.register_action(
    "echo_test.start",
    StartAction,
    automation.maybe_simple_id({cv.GenerateID(): cv.use_id(EchoTest)}),
    synchronous=True,
)
async def echo_test_start_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
