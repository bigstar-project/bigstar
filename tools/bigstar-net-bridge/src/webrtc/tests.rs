use super::{candidate_by_address, candidate_type, selected_route};

#[test]
fn classifies_candidate_types_and_routes() {
    let private_host = "a=candidate:1 1 UDP 1 192.168.0.10 5000 typ host";
    let private_peer = "a=candidate:2 1 UDP 1 192.168.0.11 5001 typ host";
    let public_host = "a=candidate:3 1 UDP 1 2001:db8::10 5000 typ host";
    let public_peer = "a=candidate:4 1 UDP 1 2001:db8::11 5001 typ host";
    let srflx = "a=candidate:5 1 UDP 1 203.0.113.10 5002 typ srflx";
    let relay = "a=candidate:6 1 UDP 1 203.0.113.20 5003 typ relay";

    assert_eq!(candidate_type(srflx), "srflx");
    assert_eq!(selected_route(private_host, private_peer), "local");
    assert_eq!(selected_route(public_host, public_peer), "direct");
    assert_eq!(selected_route(private_host, srflx), "stun");
    assert_eq!(selected_route(private_host, relay), "turn-relay");
}

#[test]
fn finds_candidate_by_selected_socket_address() {
    let candidates = vec![
        "candidate:1 1 UDP 1 192.168.0.10 5000 typ host".to_owned(),
        "a=candidate:2 1 UDP 1 2001:db8::10 5001 typ host".to_owned(),
    ];

    assert_eq!(
        candidate_by_address(&candidates, "192.168.0.10:5000"),
        Some("candidate:1 1 UDP 1 192.168.0.10 5000 typ host")
    );
    assert_eq!(
        candidate_by_address(&candidates, "[2001:db8::10]:5001"),
        Some("a=candidate:2 1 UDP 1 2001:db8::10 5001 typ host")
    );
}
