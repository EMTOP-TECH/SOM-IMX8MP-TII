#ifndef NETDEV_PCS_H
#define NETDEV_PCS_H

#include <linux/phy.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct device_node;
struct ethtool_cmd;
struct fwnode_handle;
struct net_device;
struct phylink;

enum {
	MLO_PAUSE_NONE,
	MLO_PAUSE_RX = BIT(0),
	MLO_PAUSE_TX = BIT(1),
	MLO_PAUSE_TXRX_MASK = MLO_PAUSE_TX | MLO_PAUSE_RX,
	MLO_PAUSE_AN = BIT(2),

	MLO_AN_PHY = 0,	/* Conventional PHY */
	MLO_AN_FIXED,	/* Fixed-link mode */
	MLO_AN_INBAND,	/* In-band protocol */
	MLO_AN_C73,	/* Clause 73 autoneg selects technology-dependent PCS */

	/* PCS "negotiation" mode.
	 *  PHYLINK_PCS_NEG_NONE - protocol has no inband capability
	 *  PHYLINK_PCS_NEG_OUTBAND - some out of band or fixed link setting
	 *  PHYLINK_PCS_NEG_INBAND_DISABLED - inband mode disabled, e.g.
	 *				      1000base-X with autoneg off
	 *  PHYLINK_PCS_NEG_INBAND_ENABLED - inband mode enabled
	 *  PHYLINK_PCS_NEG_C73_DISABLED - forced backplane speed
	 *				   (clause 73 autoneg off)
	 *  PHYLINK_PCS_NEG_C73_ENABLED - clause 73 autoneg enabled
	 * Additionally, this can be tested using bitmasks:
	 *  PHYLINK_PCS_NEG_INBAND - inband mode selected
	 *  PHYLINK_PCS_NEG_C73 - clause 73 autoneg mode selected
	 *  PHYLINK_PCS_NEG_ENABLED - negotiation mode enabled
	 */
	PHYLINK_PCS_NEG_NONE = 0,
	PHYLINK_PCS_NEG_ENABLED = BIT(4),
	PHYLINK_PCS_NEG_OUTBAND = BIT(5),
	PHYLINK_PCS_NEG_INBAND = BIT(6),
	PHYLINK_PCS_NEG_INBAND_DISABLED = PHYLINK_PCS_NEG_INBAND,
	PHYLINK_PCS_NEG_INBAND_ENABLED = PHYLINK_PCS_NEG_INBAND |
					 PHYLINK_PCS_NEG_ENABLED,
	PHYLINK_PCS_NEG_C73 = BIT(7),
	PHYLINK_PCS_NEG_C73_DISABLED = PHYLINK_PCS_NEG_C73,
	PHYLINK_PCS_NEG_C73_ENABLED = PHYLINK_PCS_NEG_C73 |
				      PHYLINK_PCS_NEG_ENABLED,

	/* MAC_SYM_PAUSE and MAC_ASYM_PAUSE are used when configuring our
	 * autonegotiation advertisement. They correspond to the PAUSE and
	 * ASM_DIR bits defined by 802.3, respectively.
	 *
	 * The following table lists the values of tx_pause and rx_pause which
	 * might be requested in mac_link_up. The exact values depend on either
	 * the results of autonegotation (if MLO_PAUSE_AN is set) or user
	 * configuration (if MLO_PAUSE_AN is not set).
	 *
	 * MAC_SYM_PAUSE MAC_ASYM_PAUSE MLO_PAUSE_AN tx_pause/rx_pause
	 * ============= ============== ============ ==================
	 *             0              0            0 0/0
	 *             0              0            1 0/0
	 *             0              1            0 0/0, 0/1, 1/0, 1/1
	 *             0              1            1 0/0,      1/0
	 *             1              0            0 0/0,           1/1
	 *             1              0            1 0/0,           1/1
	 *             1              1            0 0/0, 0/1, 1/0, 1/1
	 *             1              1            1 0/0, 0/1,      1/1
	 *
	 * If you set MAC_ASYM_PAUSE, the user may request any combination of
	 * tx_pause and rx_pause. You do not have to support these
	 * combinations.
	 *
	 * However, you should support combinations of tx_pause and rx_pause
	 * which might be the result of autonegotation. For example, don't set
	 * MAC_SYM_PAUSE unless your device can support tx_pause and rx_pause
	 * at the same time.
	 */
	MAC_SYM_PAUSE	= BIT(0),
	MAC_ASYM_PAUSE	= BIT(1),
	MAC_10HD	= BIT(2),
	MAC_10FD	= BIT(3),
	MAC_10		= MAC_10HD | MAC_10FD,
	MAC_100HD	= BIT(4),
	MAC_100FD	= BIT(5),
	MAC_100		= MAC_100HD | MAC_100FD,
	MAC_1000HD	= BIT(6),
	MAC_1000FD	= BIT(7),
	MAC_1000	= MAC_1000HD | MAC_1000FD,
	MAC_2500FD	= BIT(8),
	MAC_5000FD	= BIT(9),
	MAC_10000FD	= BIT(10),
	MAC_20000FD	= BIT(11),
	MAC_25000FD	= BIT(12),
	MAC_40000FD	= BIT(13),
	MAC_50000FD	= BIT(14),
	MAC_56000FD	= BIT(15),
	MAC_100000FD	= BIT(16),
	MAC_200000FD	= BIT(17),
	MAC_400000FD	= BIT(18),
};

static inline bool phylink_autoneg_inband(unsigned int mode)
{
	return mode == MLO_AN_INBAND;
}

static inline bool phylink_autoneg_c73(unsigned int mode)
{
	return mode == MLO_AN_C73;
}

static inline bool phylink_autoneg_any(unsigned int mode)
{
	return phylink_autoneg_inband(mode) || phylink_autoneg_c73(mode);
}

/**
 * struct phylink_link_state - link state structure
 * @advertising: ethtool bitmask containing advertised link modes
 * @lp_advertising: ethtool bitmask containing link partner advertised link
 *   modes
 * @interface: link &typedef phy_interface_t mode
 * @speed: link speed, one of the SPEED_* constants.
 * @duplex: link duplex mode, one of DUPLEX_* constants.
 * @pause: link pause state, described by MLO_PAUSE_* constants.
 * @rate_matching: rate matching being performed, one of the RATE_MATCH_*
 *   constants. If rate matching is taking place, then the speed/duplex of
 *   the medium link mode (@speed and @duplex) and the speed/duplex of the phy
 *   interface mode (@interface) are different.
 * @link: true if the link is up.
 * @an_complete: true if autonegotiation has completed.
 */
struct phylink_link_state {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertising);
	phy_interface_t interface;
	int speed;
	int duplex;
	int pause;
	int rate_matching;
	unsigned int link:1;
	unsigned int an_complete:1;
};

enum phylink_op_type {
	PHYLINK_NETDEV = 0,
	PHYLINK_DEV,
};

/**
 * struct phylink_config - PHYLINK configuration structure
 * @dev: a pointer to a struct device associated with the MAC
 * @type: operation type of PHYLINK instance
 * @poll_fixed_state: if true, starts link_poll,
 *		      if MAC link is at %MLO_AN_FIXED mode.
 * @mac_managed_pm: if true, indicate the MAC driver is responsible for PHY PM.
 * @mac_requires_rxc: if true, the MAC always requires a receive clock from PHY.
 *                    The PHY driver should start the clock signal as soon as
 *                    possible and avoid stopping it during suspend events.
 * @default_an_inband: if true, defaults to MLO_AN_INBAND rather than
 *		       MLO_AN_PHY. A fixed-link specification will override.
 * @get_fixed_state: callback to execute to determine the fixed link state,
 *		     if MAC link is at %MLO_AN_FIXED mode.
 * @supported_interfaces: bitmap describing which PHY_INTERFACE_MODE_xxx
 *                        are supported by the MAC/PCS.
 * @mac_capabilities: MAC pause/speed/duplex capabilities.
 * @cfg_link_an_mode: see %phylink_pcs. Necessary for %mac_select_pcs.
 */
struct phylink_config {
	struct device *dev;
	enum phylink_op_type type;
	bool poll_fixed_state;
	bool mac_managed_pm;
	bool mac_requires_rxc;
	bool default_an_inband;
	void (*get_fixed_state)(struct phylink_config *config,
				struct phylink_link_state *state);
	DECLARE_PHY_INTERFACE_MASK(supported_interfaces);
	unsigned long mac_capabilities;
	unsigned int cfg_link_an_mode;
};

void phylink_limit_mac_speed(struct phylink_config *config, u32 max_speed);

int phylink_interface_max_speed(phy_interface_t interface);

/**
 * struct phylink_mac_ops - MAC operations structure.
 * @mac_get_caps: Get MAC capabilities for interface mode.
 * @mac_select_pcs: Select a PCS for the interface mode.
 * @mac_prepare: prepare for a major reconfiguration of the interface.
 * @mac_config: configure the MAC for the selected mode and state.
 * @mac_finish: finish a major reconfiguration of the interface.
 * @mac_link_down: take the link down.
 * @mac_link_up: allow the link to come up.
 *
 * The individual methods are described more fully below.
 */
struct phylink_mac_ops {
	unsigned long (*mac_get_caps)(struct phylink_config *config,
				      phy_interface_t interface);
	struct phylink_pcs *(*mac_select_pcs)(struct phylink_config *config,
					      phy_interface_t interface);
	int (*mac_prepare)(struct phylink_config *config, unsigned int mode,
			   phy_interface_t iface);
	void (*mac_config)(struct phylink_config *config, unsigned int mode,
			   const struct phylink_link_state *state);
	int (*mac_finish)(struct phylink_config *config, unsigned int mode,
			  phy_interface_t iface);
	void (*mac_link_down)(struct phylink_config *config, unsigned int mode,
			      phy_interface_t interface);
	void (*mac_link_up)(struct phylink_config *config,
			    struct phy_device *phy, unsigned int mode,
			    phy_interface_t interface, int speed, int duplex,
			    bool tx_pause, bool rx_pause);
};

#if 0 /* For kernel-doc purposes only. */
/**
 * mac_get_caps: Get MAC capabilities for interface mode.
 * @config: a pointer to a &struct phylink_config.
 * @interface: PHY interface mode.
 *
 * Optional method. When not provided, config->mac_capabilities will be used.
 * When implemented, this returns the MAC capabilities for the specified
 * interface mode where there is some special handling required by the MAC
 * driver (e.g. not supporting half-duplex in certain interface modes.)
 */
unsigned long mac_get_caps(struct phylink_config *config,
			   phy_interface_t interface);
/**
 * mac_select_pcs: Select a PCS for the interface mode.
 * @config: a pointer to a &struct phylink_config.
 * @interface: PHY interface mode for PCS
 *
 * Return the &struct phylink_pcs for the specified interface mode, or
 * NULL if none is required, or an error pointer on error.
 *
 * This must not modify any state. It is used to query which PCS should
 * be used. Phylink will use this during validation to ensure that the
 * configuration is valid, and when setting a configuration to internally
 * set the PCS that will be used.
 */
struct phylink_pcs *mac_select_pcs(struct phylink_config *config,
				   phy_interface_t interface);

/**
 * mac_prepare() - prepare to change the PHY interface mode
 * @config: a pointer to a &struct phylink_config.
 * @mode: one of %MLO_AN_FIXED, %MLO_AN_PHY, %MLO_AN_INBAND.
 * @iface: interface mode to switch to
 *
 * phylink will call this method at the beginning of a full initialisation
 * of the link, which includes changing the interface mode or at initial
 * startup time. It may be called for the current mode. The MAC driver
 * should perform whatever actions are required, e.g. disabling the
 * Serdes PHY.
 *
 * This will be the first call in the sequence:
 * - mac_prepare()
 * - mac_config()
 * - pcs_config()
 * - possible pcs_an_restart()
 * - mac_finish()
 *
 * Returns zero on success, or negative errno on failure which will be
 * reported to the kernel log.
 */
int mac_prepare(struct phylink_config *config, unsigned int mode,
		phy_interface_t iface);

/**
 * mac_config() - configure the MAC for the selected mode and state
 * @config: a pointer to a &struct phylink_config.
 * @mode: one of %MLO_AN_FIXED, %MLO_AN_PHY, %MLO_AN_INBAND.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Note - not all members of @state are valid.  In particular,
 * @state->lp_advertising, @state->link, @state->an_complete are never
 * guaranteed to be correct, and so any mac_config() implementation must
 * never reference these fields.
 *
 * This will only be called to reconfigure the MAC for a "major" change in
 * e.g. interface mode. It will not be called for changes in speed, duplex
 * or pause modes or to change the in-band advertisement.
 *
 * In all negotiation modes, as defined by @mode, @state->pause indicates the
 * pause settings which should be applied as follows. If %MLO_PAUSE_AN is not
 * set, %MLO_PAUSE_TX and %MLO_PAUSE_RX indicate whether the MAC should send
 * pause frames and/or act on received pause frames respectively. Otherwise,
 * the results of in-band negotiation/status from the MAC PCS should be used
 * to control the MAC pause mode settings.
 *
 * The action performed depends on the currently selected mode:
 *
 * %MLO_AN_FIXED, %MLO_AN_PHY:
 *   Configure for non-inband negotiation mode, where the link settings
 *   are completely communicated via mac_link_up().  The physical link
 *   protocol from the MAC is specified by @state->interface.
 *
 *   @state->advertising may be used, but is not required.
 *
 *   Older drivers (prior to the mac_link_up() change) may use @state->speed,
 *   @state->duplex and @state->pause to configure the MAC, but this is
 *   deprecated; such drivers should be converted to use mac_link_up().
 *
 *   Other members of @state must be ignored.
 *
 *   Valid state members: interface, advertising.
 *   Deprecated state members: speed, duplex, pause.
 *
 * %MLO_AN_INBAND:
 *   place the link in an inband negotiation mode (such as 802.3z
 *   1000base-X or Cisco SGMII mode depending on the @state->interface
 *   mode). In both cases, link state management (whether the link
 *   is up or not) is performed by the MAC, and reported via the
 *   pcs_get_state() callback. Changes in link state must be made
 *   by calling phylink_mac_change().
 *
 *   Interface mode specific details are mentioned below.
 *
 *   If in 802.3z mode, the link speed is fixed, dependent on the
 *   @state->interface. Duplex and pause modes are negotiated via
 *   the in-band configuration word. Advertised pause modes are set
 *   according to the @state->an_enabled and @state->advertising
 *   flags. Beware of MACs which only support full duplex at gigabit
 *   and higher speeds.
 *
 *   If in Cisco SGMII mode, the link speed and duplex mode are passed
 *   in the serial bitstream 16-bit configuration word, and the MAC
 *   should be configured to read these bits and acknowledge the
 *   configuration word. Nothing is advertised by the MAC. The MAC is
 *   responsible for reading the configuration word and configuring
 *   itself accordingly.
 *
 *   Valid state members: interface, an_enabled, pause, advertising.
 *
 * %MLO_AN_C73:
 *   The link uses IEEE 802.3 clause 73 auto-negotiation to select a
 *   PCS according to the common technology resolved with the far end.
 *   In this mode, state->interface is also not valid.
 *
 * Implementations are expected to update the MAC to reflect the
 * requested settings - i.o.w., if nothing has changed between two
 * calls, no action is expected.  If only flow control settings have
 * changed, flow control should be updated *without* taking the link
 * down.  This "update" behaviour is critical to avoid bouncing the
 * link up status.
 */
void mac_config(struct phylink_config *config, unsigned int mode,
		const struct phylink_link_state *state);

/**
 * mac_finish() - finish a to change the PHY interface mode
 * @config: a pointer to a &struct phylink_config.
 * @mode: one of %MLO_AN_FIXED, %MLO_AN_PHY, %MLO_AN_INBAND, %MLO_AN_C73.
 * @iface: interface mode to switch to
 *
 * phylink will call this if it called mac_prepare() to allow the MAC to
 * complete any necessary steps after the MAC and PCS have been configured
 * for the @mode and @iface. E.g. a MAC driver may wish to re-enable the
 * Serdes PHY here if it was previously disabled by mac_prepare().
 *
 * Returns zero on success, or negative errno on failure which will be
 * reported to the kernel log.
 */
int mac_finish(struct phylink_config *config, unsigned int mode,
		phy_interface_t iface);

/**
 * mac_link_down() - take the link down
 * @config: a pointer to a &struct phylink_config.
 * @mode: link autonegotiation mode
 * @interface: link &typedef phy_interface_t mode
 *
 * If @mode is not an in-band negotiation mode (as defined by
 * phylink_autoneg_inband()), force the link down and disable any
 * Energy Efficient Ethernet MAC configuration. Interface type
 * selection must be done in mac_config().
 */
void mac_link_down(struct phylink_config *config, unsigned int mode,
		   phy_interface_t interface);

/**
 * mac_link_up() - allow the link to come up
 * @config: a pointer to a &struct phylink_config.
 * @phy: any attached phy
 * @mode: link autonegotiation mode
 * @interface: link &typedef phy_interface_t mode
 * @speed: link speed
 * @duplex: link duplex
 * @tx_pause: link transmit pause enablement status
 * @rx_pause: link receive pause enablement status
 *
 * Configure the MAC for an established link.
 *
 * @speed, @duplex, @tx_pause and @rx_pause indicate the finalised link
 * settings, and should be used to configure the MAC block appropriately
 * where these settings are not automatically conveyed from the PCS block,
 * or if in-band negotiation (as defined by phylink_autoneg_inband(@mode))
 * is disabled.
 *
 * Note that when 802.3z in-band negotiation is in use, it is possible
 * that the user wishes to override the pause settings, and this should
 * be allowed when considering the implementation of this method.
 *
 * If in-band negotiation mode is disabled, allow the link to come up. If
 * @phy is non-%NULL, configure Energy Efficient Ethernet by calling
 * phy_init_eee() and perform appropriate MAC configuration for EEE.
 * Interface type selection must be done in mac_config().
 */
void mac_link_up(struct phylink_config *config, struct phy_device *phy,
		 unsigned int mode, phy_interface_t interface,
		 int speed, int duplex, bool tx_pause, bool rx_pause);
#endif

struct phylink_pcs_ops;

/**
 * struct phylink_pcs - PHYLINK PCS instance
 * @ops: a pointer to the &struct phylink_pcs_ops structure
 * @phylink: pointer to &struct phylink_config
 * @neg_mode: provide PCS neg mode via "mode" argument
 * @poll: poll the PCS for link changes
 * @rxc_always_on: The MAC driver requires the reference clock
 *                 to always be on. Standalone PCS drivers which
 *                 do not have access to a PHY device can check
 *                 this instead of PHY_F_RXC_ALWAYS_ON.
 * @cfg_link_an_mode: phylink sets this to the statically configured link
 *		      autoneg mode (%MLO_AN_FIXED, %MLO_AN_PHY, %MLO_AN_INBAND,
 *		      %MLO_AN_C73). Note that in some cases, this may change at
 *		      runtime (from %MLO_AN_INBAND to %MLO_AN_PHY).
 *
 * This structure is designed to be embedded within the PCS private data,
 * and will be passed between phylink and the PCS.
 *
 * The @phylink member is private to phylink and must not be touched by
 * the PCS driver.
 */
struct phylink_pcs {
	const struct phylink_pcs_ops *ops;
	struct phylink *phylink;
	bool neg_mode;
	bool poll;
	bool rxc_always_on;
	unsigned int cfg_link_an_mode;
};

/**
 * struct phylink_pcs_ops - MAC PCS operations structure.
 * @pcs_validate: validate the link configuration.
 * @pcs_enable: enable the PCS.
 * @pcs_disable: disable the PCS.
 * @pcs_pre_config: pre-mac_config method (for errata)
 * @pcs_post_config: post-mac_config method (for arrata)
 * @pcs_get_state: read the current MAC PCS link state from the hardware.
 * @pcs_config: configure the MAC PCS for the selected mode and state.
 * @pcs_an_restart: restart 802.3z BaseX autonegotiation.
 * @pcs_link_up: program the PCS for the resolved link configuration
 *               (where necessary).
 * @pcs_pre_init: configure PCS components necessary for MAC hardware
 *                initialization e.g. RX clock for stmmac.
 */
struct phylink_pcs_ops {
	int (*pcs_validate)(struct phylink_pcs *pcs, unsigned long *supported,
			    const struct phylink_link_state *state);
	int (*pcs_enable)(struct phylink_pcs *pcs);
	void (*pcs_disable)(struct phylink_pcs *pcs);
	void (*pcs_pre_config)(struct phylink_pcs *pcs,
			       phy_interface_t interface);
	int (*pcs_post_config)(struct phylink_pcs *pcs,
			       phy_interface_t interface);
	void (*pcs_get_state)(struct phylink_pcs *pcs,
			      struct phylink_link_state *state);
	int (*pcs_config)(struct phylink_pcs *pcs, unsigned int neg_mode,
			  phy_interface_t interface,
			  const unsigned long *advertising,
			  bool permit_pause_to_mac);
	void (*pcs_an_restart)(struct phylink_pcs *pcs);
	void (*pcs_link_up)(struct phylink_pcs *pcs, unsigned int neg_mode,
			    phy_interface_t interface, int speed, int duplex);
	int (*pcs_pre_init)(struct phylink_pcs *pcs);
};

#if 0 /* For kernel-doc purposes only. */
/**
 * pcs_validate() - validate the link configuration.
 * @pcs: a pointer to a &struct phylink_pcs.
 * @supported: ethtool bitmask for supported link modes.
 * @state: a const pointer to a &struct phylink_link_state.
 *
 * Validate the interface mode, and advertising's autoneg bit, removing any
 * media ethtool link modes that would not be supportable from the supported
 * mask. Phylink will propagate the changes to the advertising mask. See the
 * &struct phylink_mac_ops validate() method.
 *
 * Returns -EINVAL if the interface mode/autoneg mode is not supported.
 * Returns non-zero positive if the link state can be supported.
 */
int pcs_validate(struct phylink_pcs *pcs, unsigned long *supported,
		 const struct phylink_link_state *state);

/**
 * pcs_enable() - enable the PCS.
 * @pcs: a pointer to a &struct phylink_pcs.
 */
int pcs_enable(struct phylink_pcs *pcs);

/**
 * pcs_disable() - disable the PCS.
 * @pcs: a pointer to a &struct phylink_pcs.
 */
void pcs_disable(struct phylink_pcs *pcs);

/**
 * pcs_get_state() - Read the current inband link state from the hardware
 * @pcs: a pointer to a &struct phylink_pcs.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Read the current inband link state from the MAC PCS, reporting the
 * current speed in @state->speed, duplex mode in @state->duplex, pause
 * mode in @state->pause using the %MLO_PAUSE_RX and %MLO_PAUSE_TX bits,
 * negotiation completion state in @state->an_complete, and link up state
 * in @state->link. If possible, @state->lp_advertising should also be
 * populated.
 */
void pcs_get_state(struct phylink_pcs *pcs,
		   struct phylink_link_state *state);

/**
 * pcs_config() - Configure the PCS mode and advertisement
 * @pcs: a pointer to a &struct phylink_pcs.
 * @neg_mode: link negotiation mode (see below)
 * @interface: interface mode to be used
 * @advertising: adertisement ethtool link mode mask
 * @permit_pause_to_mac: permit forwarding pause resolution to MAC
 *
 * Configure the PCS for the operating mode, the interface mode, and set
 * the advertisement mask. @permit_pause_to_mac indicates whether the
 * hardware may forward the pause mode resolution to the MAC.
 *
 * When operating in %MLO_AN_INBAND, inband should always be enabled,
 * otherwise inband should be disabled.
 *
 * For SGMII, there is no advertisement from the MAC side, the PCS should
 * be programmed to acknowledge the inband word from the PHY.
 *
 * For 1000BASE-X, the advertisement should be programmed into the PCS.
 *
 * For most 10GBASE-R, there is no advertisement.
 *
 * For %MLO_AN_C73, the "interface" argument should be ignored, since clause 73
 * autonegotiation has not started, and thus, it has not resolved a link mode
 * yet, either.
 *
 * The %neg_mode argument should be tested via the phylink_mode_*() family of
 * functions, or for PCS that set pcs->neg_mode true, should be tested
 * against the PHYLINK_PCS_NEG_* definitions.
 */
int pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
	       phy_interface_t interface, const unsigned long *advertising,
	       bool permit_pause_to_mac);

/**
 * pcs_an_restart() - restart 802.3z BaseX autonegotiation
 * @pcs: a pointer to a &struct phylink_pcs.
 *
 * When PCS ops are present, this overrides mac_an_restart() in &struct
 * phylink_mac_ops.
 */
void pcs_an_restart(struct phylink_pcs *pcs);

/**
 * pcs_link_up() - program the PCS for the resolved link configuration
 * @pcs: a pointer to a &struct phylink_pcs.
 * @neg_mode: link negotiation mode (see below)
 * @interface: link &typedef phy_interface_t mode
 * @speed: link speed
 * @duplex: link duplex
 *
 * This call will be made just before mac_link_up() to inform the PCS of
 * the resolved link parameters. For example, a PCS operating in SGMII
 * mode without in-band AN needs to be manually configured for the link
 * and duplex setting. Otherwise, this should be a no-op.
 *
 * The %mode argument should be tested via the phylink_mode_*() family of
 * functions, or for PCS that set pcs->neg_mode true, should be tested
 * against the PHYLINK_PCS_NEG_* definitions.
 */
void pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
		 phy_interface_t interface, int speed, int duplex);

/**
 * pcs_pre_init() - Configure PCS components necessary for MAC initialization
 * @pcs: a pointer to a &struct phylink_pcs.
 *
 * This function can be called by MAC drivers through the
 * phylink_pcs_pre_init() wrapper, before their hardware is initialized. It
 * should not be called after the link is brought up, as reconfiguring the PCS
 * at this point could break the link.
 *
 * Some MAC devices require specific hardware initialization to be performed by
 * their associated PCS device before they can properly initialize their own
 * hardware. An example of this is the initialization of stmmac controllers,
 * which requires an active REF_CLK signal to be provided by the PHY/PCS.
 *
 * By calling phylink_pcs_pre_init(), MAC drivers can ensure that the PCS is
 * setup in a way that allows for successful hardware initialization.
 *
 * The specific configuration performed by pcs_pre_init() is dependent on the
 * model of PCS and the requirements of the MAC device attached to it. PCS
 * driver authors should consider whether their target device is to be used in
 * conjunction with a MAC device whose driver calls phylink_pcs_pre_init(). MAC
 * driver authors should document their requirements for the PCS
 * pre-initialization.
 *
 */
int pcs_pre_init(struct phylink_pcs *pcs);

#endif

struct phylink *phylink_create(struct phylink_config *,
			       const struct fwnode_handle *,
			       phy_interface_t,
			       const struct phylink_mac_ops *);
void phylink_destroy(struct phylink *);
bool phylink_expects_phy(struct phylink *pl);

int phylink_connect_phy(struct phylink *, struct phy_device *);
int phylink_of_phy_connect(struct phylink *, struct device_node *, u32 flags);
int phylink_fwnode_phy_connect(struct phylink *pl,
			       const struct fwnode_handle *fwnode,
			       u32 flags);
void phylink_disconnect_phy(struct phylink *);
int phylink_set_fixed_link(struct phylink *,
			   const struct phylink_link_state *);

void phylink_mac_change(struct phylink *, bool up);
void phylink_pcs_change(struct phylink_pcs *, bool up);

int phylink_pcs_pre_init(struct phylink *pl, struct phylink_pcs *pcs);

void phylink_start(struct phylink *);
void phylink_stop(struct phylink *);

void phylink_suspend(struct phylink *pl, bool mac_wol);
void phylink_resume(struct phylink *pl);

void phylink_ethtool_get_wol(struct phylink *, struct ethtool_wolinfo *);
int phylink_ethtool_set_wol(struct phylink *, struct ethtool_wolinfo *);

int phylink_ethtool_ksettings_get(struct phylink *,
				  struct ethtool_link_ksettings *);
int phylink_ethtool_ksettings_set(struct phylink *,
				  const struct ethtool_link_ksettings *);
int phylink_ethtool_nway_reset(struct phylink *);
void phylink_ethtool_get_pauseparam(struct phylink *,
				    struct ethtool_pauseparam *);
int phylink_ethtool_set_pauseparam(struct phylink *,
				   struct ethtool_pauseparam *);
int phylink_get_eee_err(struct phylink *);
int phylink_init_eee(struct phylink *, bool);
int phylink_ethtool_get_eee(struct phylink *link, struct ethtool_keee *eee);
int phylink_ethtool_set_eee(struct phylink *link, struct ethtool_keee *eee);
int phylink_mii_ioctl(struct phylink *, struct ifreq *, int);
int phylink_speed_down(struct phylink *pl, bool sync);
int phylink_speed_up(struct phylink *pl);

#define phylink_zero(bm) \
	bitmap_zero(bm, __ETHTOOL_LINK_MODE_MASK_NBITS)
#define __phylink_do_bit(op, bm, mode) \
	op(ETHTOOL_LINK_MODE_ ## mode ## _BIT, bm)

#define phylink_set(bm, mode)	__phylink_do_bit(__set_bit, bm, mode)
#define phylink_clear(bm, mode)	__phylink_do_bit(__clear_bit, bm, mode)
#define phylink_test(bm, mode)	__phylink_do_bit(test_bit, bm, mode)

void phylink_set_port_modes(unsigned long *bits);

/**
 * phylink_get_link_timer_ns - return the PCS link timer value
 * @interface: link &typedef phy_interface_t mode
 *
 * Return the PCS link timer setting in nanoseconds for the PHY @interface
 * mode, or -EINVAL if not appropriate.
 */
static inline int phylink_get_link_timer_ns(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return 1600000;

	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return 10000000;

	default:
		return -EINVAL;
	}
}

void phylink_mii_c22_pcs_decode_state(struct phylink_link_state *state,
				      u16 bmsr, u16 lpa);
void phylink_mii_c22_pcs_get_state(struct mdio_device *pcs,
				   struct phylink_link_state *state);
int phylink_mii_c22_pcs_encode_advertisement(phy_interface_t interface,
					     const unsigned long *advertising);
int phylink_mii_c22_pcs_config(struct mdio_device *pcs,
			       phy_interface_t interface,
			       const unsigned long *advertising,
			       unsigned int neg_mode);
void phylink_mii_c22_pcs_an_restart(struct mdio_device *pcs);

void phylink_resolve_c73(struct phylink_link_state *state);

void phylink_mii_c45_pcs_get_state(struct mdio_device *pcs,
				   struct phylink_link_state *state);

void phylink_decode_usxgmii_word(struct phylink_link_state *state,
				 uint16_t lpa);
#endif
